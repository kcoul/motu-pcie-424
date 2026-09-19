#!/usr/bin/env python3
"""Log and probe CueMix FX's OSC interface.

CueMix FX runs an OSC server (port from `defaults read com.motu.CueMixFX`,
key OSCUDPPort) and browses Bonjour `_osc._udp` for clients. It keeps two
address spaces, which is the thing to know before reading any capture:

  host tree     /bin/<bus>/<ch>/cdf        what the console's Value tree calls it
  client tree   /mix/fader/1/1             what a layout's `routing=` maps it to

`OSCDevice::ApplyRoutingHostToClient` rewrites one into the other on the way
out, so what appears on the wire depends on the client layout in force.

OSC is implemented in DeviceListManager/DeviceController, not in any hardware
back end, so this works against a PCI-424 as well as a FireWire/USB interface.
Card-level findings live in docs/CUEMIX-API.md.

How CueMix FX's OSC actually works (from OSCServer/OSCZeroConf and the prefs):

  * CueMix FX is the SERVER. It advertises itself on Bonjour as
    `_osc._udp` named "<device> CueMix FX OSC on <host>", port = OSCUDPPort.
  * It also BROWSES `_osc._udp` for clients, and lists what it finds in
    Control Surfaces > Configure OSC Devices….
  * A client is identified by its Bonjour SERVICE NAME, not an address --
    `OSCServer::ResolveIpEndpoint` looks it up at send time -- and is stored
    with a layout description file (`OSCLayoutDescriptionFilename`).
  * There is NO wire handshake. Unsolicited messages get no reply. Until a
    client is added in that dialog, nothing is pushed. The choice persists in
    preferences, so it is a one-time step per machine.

So: run `listen --advertise`, add the advertised name in that dialog, choose
TouchOSC-iPad.layout_description, and values start arriving.

  tools/osc-log.py tree                     address tree from the bundled layout
  tools/osc-log.py discover                 find the CueMix OSC server on the network
  tools/osc-log.py prefs                    show saved OSC client/page/meter state
  tools/osc-log.py listen --advertise NAME  advertise, then receive and decode
  tools/osc-log.py send /addr f:0.5         send one message, log any reply
  tools/osc-log.py probe                    try known addresses (expect silence)
"""

import argparse, csv as csvmod, os, plistlib, re, socket, struct, subprocess, sys, time

LAYOUT = "/Applications/CueMix FX.app/Contents/Resources/TouchOSC-iPad.layout_description"
PREFS  = os.path.expanduser("~/Library/Preferences/com.motu.CueMixFX.plist")


# ---------------------------------------------------------------- OSC codec

def pad4(n):
    return (4 - (n & 3)) & 3


def _read_string(buf, i):
    """OSC string: NUL-terminated, then padded so total length is a multiple of 4."""
    end = buf.index(b"\0", i)
    s = buf[i:end].decode("utf-8", "replace")
    n = end - i + 1
    n += pad4(n)
    return s, i + n


def _read_blob(buf, i):
    (size,) = struct.unpack_from(">i", buf, i)
    i += 4
    data = buf[i:i + size]
    return data, i + size + pad4(size)


def decode(buf):
    """Return a list of (timetag_or_None, address, typetag, [args])."""
    if buf.startswith(b"#bundle\0"):
        (sec, frac) = struct.unpack_from(">II", buf, 8)
        tt = None if (sec, frac) == (0, 1) else sec + frac / 2**32
        out, i = [], 16
        while i + 4 <= len(buf):
            (size,) = struct.unpack_from(">i", buf, i)
            i += 4
            if size <= 0 or i + size > len(buf):
                break
            for m in decode(buf[i:i + size]):
                out.append((tt if m[0] is None else m[0],) + m[1:])
            i += size
        return out

    if not buf.startswith(b"/"):
        return [(None, "<malformed>", "", [buf])]

    addr, i = _read_string(buf, 0)
    if i >= len(buf):
        return [(None, addr, "", [])]
    tags, i = _read_string(buf, i)
    args = []
    for t in tags.lstrip(","):
        try:
            if t == "i":
                args.append(struct.unpack_from(">i", buf, i)[0]); i += 4
            elif t == "f":
                args.append(struct.unpack_from(">f", buf, i)[0]); i += 4
            elif t == "d":
                args.append(struct.unpack_from(">d", buf, i)[0]); i += 8
            elif t == "h":
                args.append(struct.unpack_from(">q", buf, i)[0]); i += 8
            elif t == "t":
                args.append(struct.unpack_from(">Q", buf, i)[0]); i += 8
            elif t == "s" or t == "S":
                s, i = _read_string(buf, i); args.append(s)
            elif t == "b":
                b, i = _read_blob(buf, i); args.append(b)
            elif t == "T":
                args.append(True)
            elif t == "F":
                args.append(False)
            elif t == "N":
                args.append(None)
            elif t == "I":
                args.append(float("inf"))
            else:
                args.append(f"<?{t}>")
        except struct.error:
            args.append("<truncated>")
            break
    return [(None, addr, tags, args)]


def _enc_string(s):
    b = s.encode("utf-8") + b"\0"
    return b + b"\0" * pad4(len(b))


def encode(addr, args):
    """args: list of (tag, value)."""
    tags = "," + "".join(t for t, _ in args)
    out = _enc_string(addr) + _enc_string(tags)
    for t, v in args:
        if t == "i":
            out += struct.pack(">i", int(v))
        elif t == "f":
            out += struct.pack(">f", float(v))
        elif t == "d":
            out += struct.pack(">d", float(v))
        elif t == "s":
            out += _enc_string(str(v))
        elif t in "TFN":
            pass
    return out


# ---------------------------------------------------------------- helpers

def server_port(default=8000):
    try:
        with open(PREFS, "rb") as f:
            p = plistlib.load(f)
    except Exception:
        return default
    for k, v in p.items():
        if k == "OSCUDPPort":
            return int(v)
        if isinstance(v, dict) and "OSCUDPPort" in v:
            return int(v["OSCUDPPort"])
    return default


def fmt(args, raw=False):
    """Human-readable by default; raw=True for CSV (no repr quoting)."""
    out = []
    for a in args:
        if isinstance(a, float):
            out.append(f"{a:.6g}")
        elif isinstance(a, bytes):
            out.append(a.hex() if raw else f"<{len(a)} bytes>")
        elif isinstance(a, str):
            out.append(a if raw else repr(a))
        else:
            out.append(str(a))
    return " ".join(out)


# ---------------------------------------------------------------- commands

def cmd_tree(a):
    """The address tree, straight out of the shipped layout description."""
    try:
        xml = open(LAYOUT, encoding="utf-8", errors="replace").read()
    except OSError as e:
        sys.exit(f"cannot read layout: {e}")

    tok = re.compile(
        r'<XSET\s+type="(?P<xt>\w+)"(?:\s+name="(?P<xn>[^"]*)")?'
        r'|</XSET>'
        r'|<TEXT\s+name="(?P<tn>[^"]*)"\s+type="(?P<tt>\w+)"(?P<at>[^>]*)>'
        r'(?P<ad>[^<]*)</TEXT>')

    stack, page, groups, order = [], None, {}, []
    for m in tok.finditer(xml):
        if m.group(0) == "</XSET>":
            if stack:
                stack.pop()
            continue
        if m.group("xt"):
            stack.append((m.group("xt"), m.group("xn") or ""))
            if m.group("xt") == "PAGE":
                page = m.group("xn")
            continue

        name, typ, attrs = m.group("tn"), m.group("tt"), m.group("at")
        addr = m.group("ad").strip()
        routing = re.search(r'routing="([^"]*)"', attrs)
        routing = routing.group(1) if routing else ""
        enclosing = stack[-1][0] if stack else ""

        # Inside a RADIO block the element body is the radio's *value*, not an
        # address; the address is the routing attribute.
        is_radio = enclosing == "RADIO"
        key = (page, enclosing, re.sub(r"\d+$", "", name) or name, typ,
               re.sub(r"\+\d+", "+", addr) if not is_radio else "",
               re.sub(r"\+\d+", "+", re.sub(r"/\d+(?=/|$)", "/", routing)))
        if key not in groups:
            groups[key] = {"n": 0, "first_addr": addr, "first_routing": routing,
                           "first_name": name, "values": []}
            order.append(key)
        g = groups[key]
        g["n"] += 1
        if is_radio:
            g["values"].append(addr)

    cur_page = object()
    for key in order:
        page_, enclosing, _, typ, _, _ = key
        g = groups[key]
        if page_ != cur_page:
            cur_page = page_
            print(f"\n=== PAGE {page_} ===")
        rep = f" x{g['n']}" if g["n"] > 1 else ""
        if enclosing == "RADIO":
            vals = ",".join(g["values"][:8]) + ("…" if len(g["values"]) > 8 else "")
            print(f"  {g['first_name']:<14}{rep:<5} RADIO/{typ:<8} values={vals}")
            print(f"  {'':<14}{'':<5}        client {g['first_routing']}")
            continue
        line = f"  {g['first_name']:<14}{rep:<5} {typ:<14} {g['first_addr']}"
        print(line)
        if g["first_routing"]:
            print(f"  {'':<14}{'':<5} {'':<14} ^ client {g['first_routing']}")

    print("""
Placeholders the app resolves at send time (see docs/CUEMIX-API.md):
  fvEB+N     first visible enabled bus + N    pref <page>_OSCFirstVisibleEnabledBus
  fvInCS+N   first visible input strip + N    pref <page>_OSCFirstVisibleInputChannelStrip
  fvOutCS+N  first visible output strip + N   pref <page>_OSCFirstVisibleOutputChannelStrip

  /str on a numeric address gives its display string, e.g. "-6.0 dB".
  /meters is the meter-subscription SELECT; the per-page toggles land in prefs as
  <page>_meters_/pre, /post, /rms, /meters.

Dead weight on a PCI-424: the /eq, /channel_dynamics and /reverb pages, and the
rsr/rvs (reverb send/return) addresses. Those belong to the FireWire/USB FX boxes.""")


def _open_socket(port):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", port))
    return s


def _advertise(name, port):
    """Register on Bonjour as an OSC client so CueMix FX lists us."""
    p = subprocess.Popen(
        ["/usr/bin/dns-sd", "-R", name, "_osc._udp", "local", str(port)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print(f"advertising \"{name}\" as _osc._udp on port {port} (pid {p.pid})")
    print("  -> in CueMix FX: Control Surfaces > Configure OSC Devices…, select it, Enable")
    return p


def _log_loop(sock, csv=None, meters=False, quiet_meters=False):
    started = time.time()
    counts, last_report, mrate = {}, started, 0
    w = wr = None
    if csv:
        w = open(csv, "w", buffering=1, newline="")
        wr = csvmod.writer(w)
        wr.writerow(["t", "source", "address", "typetag", "args"])
    print("listening… ctrl-C to stop\n")
    try:
        while True:
            try:
                sock.settimeout(1.0)
                data, src = sock.recvfrom(65535)
            except socket.timeout:
                continue
            t = time.time() - started
            for _, addr, tags, args in decode(data):
                counts[addr] = counts.get(addr, 0) + 1
                is_meter = ("/meters" in addr or "/pre" in addr or "/post" in addr
                            or "/rms" in addr)
                if is_meter:
                    mrate += 1
                if w:
                    wr.writerow([f"{t:.4f}", f"{src[0]}:{src[1]}", addr,
                                 tags.lstrip(","), fmt(args, raw=True)])
                if quiet_meters and is_meter:
                    continue
                print(f"{t:8.3f}  {addr:<44} {tags:<8} {fmt(args)}")
            if meters and time.time() - last_report >= 1.0:
                print(f"           -- meter msgs/s: {mrate}")
                mrate = 0
                last_report = time.time()
    except KeyboardInterrupt:
        pass
    print("\n--- addresses seen ---")
    for addr, n in sorted(counts.items(), key=lambda kv: -kv[1]):
        print(f"  {n:7d}  {addr}")
    if w:
        print(f"\nwrote {w.name}")
        w.close()


def cmd_listen(a):
    sock = _open_socket(a.port)
    adv = _advertise(a.advertise, a.port) if a.advertise else None
    try:
        _log_loop(sock, a.csv, a.meters, a.quiet_meters)
    finally:
        if adv:
            adv.terminate()


def _parse_arg(tok):
    if ":" in tok and tok.split(":", 1)[0] in ("i", "f", "d", "s"):
        t, v = tok.split(":", 1)
        return (t, v)
    if tok in ("T", "F", "N"):
        return (tok, None)
    try:
        return ("i", int(tok))
    except ValueError:
        pass
    try:
        return ("f", float(tok))
    except ValueError:
        pass
    return ("s", tok)


def cmd_send(a):
    args = [_parse_arg(t) for t in a.args]
    pkt = encode(a.address, args)
    sock = _open_socket(a.reply_port)
    sock.sendto(pkt, (a.host, a.port))
    print(f"-> {a.host}:{a.port}  {a.address} "
          f"{','.join(t for t, _ in args)} {fmt([v for _, v in args])}  "
          f"({len(pkt)} bytes)")
    sock.settimeout(a.wait)
    deadline = time.time() + a.wait
    got = False
    while time.time() < deadline:
        try:
            data, src = sock.recvfrom(65535)
        except socket.timeout:
            break
        got = True
        for _, addr, tags, vals in decode(data):
            print(f"<- {src[0]}:{src[1]}  {addr:<40} {tags:<8} {fmt(vals)}")
    if not got:
        print("<- (no reply)")


def _dns_sd(args, seconds):
    p = subprocess.Popen(["/usr/bin/dns-sd"] + args, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True)
    time.sleep(seconds)
    p.terminate()
    try:
        return p.stdout.read()
    except Exception:
        return ""


def cmd_discover(a):
    out = _dns_sd(["-B", "_osc._udp", "local"], a.wait)
    names, seen = [], set()
    for ln in out.splitlines():
        m = re.search(r"\bAdd\b.*_osc\._udp\.\s+(.*\S)", ln)
        if m and m.group(1) not in seen:
            seen.add(m.group(1))
            names.append(m.group(1))
    if not names:
        print("no _osc._udp services found. Is CueMix FX running?")
        return
    print(f"{len(names)} _osc._udp service(s):\n")
    for n in names:
        kind = "CueMix FX server" if "CueMix FX OSC" in n else "other / client"
        print(f"  {n}\n      {kind}")
        res = _dns_sd(["-L", n, "_osc._udp", "local"], a.wait)
        for ln in res.splitlines():
            m = re.search(r"can be reached at (\S+):(\d+)", ln)
            if m:
                print(f"      -> {m.group(1)} port {m.group(2)}")
                break
    print("\nThe server entry is what `send` should target; give --host/--port if it")
    print("is not this machine.")


def cmd_prefs(a):
    try:
        d = plistlib.load(open(PREFS, "rb"))
    except Exception as e:
        sys.exit(f"cannot read {PREFS}: {e}")

    engines = [k for k in d if re.match(r"com_motu_driver_\w+:", k)
               and not k.endswith(("_BusNames",))]
    print(f"prefs: {PREFS}\n")
    for k in sorted(d):
        if k.endswith("_OSCClients"):
            v = d[k]
            n = v.get("numElements", 0)
            print(f"OSC client list  [{k}]")
            print(f"  numElements = {n}" + ("   <- 0 means no client is active"
                                            if not n else ""))
            for kk in sorted(x for x in v if x != "numElements"):
                print(f"  [{kk}] {v[kk]!r}")
            print()
    for k in sorted(d):
        v = d[k]
        if not isinstance(v, dict) or "OSCLayoutDescriptionFilename" not in v:
            continue
        print(f"client state  [{k}]")
        print(f"  layout       {v.get('OSCLayoutDescriptionFilename')!r}")
        print(f"  current page {v.get('OSCCurrentPage')!r}")
        subs = {kk: vv for kk, vv in v.items() if "_meters_" in kk}
        if subs:
            print("  meter subscriptions:")
            for kk in sorted(subs):
                page, kind = kk.split("_meters_", 1)
                print(f"    {page:<20} {kind:<8} {'on' if subs[kk] else 'off'}")
        offs = {kk: vv for kk, vv in v.items() if "FirstVisible" in kk and vv}
        if offs:
            print("  non-zero strip offsets:")
            for kk in sorted(offs):
                print(f"    {kk} = {offs[kk]}")
        print()
    for k in sorted(d):
        if isinstance(d[k], dict) and "OSCUDPPort" in d[k]:
            print(f"server port      {d[k]['OSCUDPPort']}   [{k}]")
    print("\nEngine keys present (the prefix changes with the interface -- a PCI-424")
    print("will appear as com_motu_driver_PCIAudio_Engine:...):")
    for e in sorted(set(engines)):
        print(f"  {e}")


PROBES = [
    ("/CueMixOSCAPI", []),
    ("/CueMixOSCAPI", [("s", "1.0")]),
    ("/ping", []),
    ("/dev/0/0/actv", []),
    ("/in/1/in/namS", []),
    ("/bin/0/0/cdf", []),
    ("/bin/0/0/cdf/str", []),
    ("/bus/0/mix/name", []),
]


def cmd_probe(a):
    sock = _open_socket(a.reply_port)
    print(f"probing {a.host}:{a.port}, replies to :{a.reply_port}\n")
    for addr, args in PROBES:
        sock.sendto(encode(addr, args), (a.host, a.port))
        print(f"-> {addr} {fmt([v for _, v in args])}")
        sock.settimeout(0.4)
        while True:
            try:
                data, src = sock.recvfrom(65535)
            except socket.timeout:
                break
            for _, r, tags, vals in decode(data):
                print(f"   <- {r:<42} {tags:<8} {fmt(vals)}")
    print("\nNo replies means CueMix FX only pushes to clients it has been pointed at.")
    print("Use:  tools/osc-log.py listen --advertise \"motu-pcie-424 logger\"")
    print("then enable it in Control Surfaces > Configure OSC Devices…")


def main():
    port = server_port()
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    t = sub.add_parser("tree", help="address tree from the bundled layout")
    t.set_defaults(fn=cmd_tree)

    l = sub.add_parser("listen", help="receive and decode")
    l.add_argument("--port", type=int, default=9000, help="our listen port (default 9000)")
    l.add_argument("--advertise", metavar="NAME", help="register on Bonjour as _osc._udp")
    l.add_argument("--csv", metavar="FILE", help="also write every message to CSV")
    l.add_argument("--meters", action="store_true", help="report meter msgs/sec")
    l.add_argument("--quiet-meters", action="store_true", help="do not print meter messages")
    l.set_defaults(fn=cmd_listen)

    s = sub.add_parser("send", help="send one message")
    s.add_argument("address")
    s.add_argument("args", nargs="*", help="i:1 f:0.5 s:txt T F N, or bare (inferred)")
    s.add_argument("--host", default="127.0.0.1")
    s.add_argument("--port", type=int, default=port)
    s.add_argument("--reply-port", type=int, default=9000)
    s.add_argument("--wait", type=float, default=1.0)
    s.set_defaults(fn=cmd_send)

    dv = sub.add_parser("discover", help="find the CueMix OSC server via Bonjour")
    dv.add_argument("--wait", type=float, default=2.5)
    dv.set_defaults(fn=cmd_discover)

    pr = sub.add_parser("prefs", help="show saved OSC client/page/meter state")
    pr.set_defaults(fn=cmd_prefs)

    b = sub.add_parser("probe", help="try known discovery addresses")
    b.add_argument("--host", default="127.0.0.1")
    b.add_argument("--port", type=int, default=port)
    b.add_argument("--reply-port", type=int, default=9000)
    b.set_defaults(fn=cmd_probe)

    a = p.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
