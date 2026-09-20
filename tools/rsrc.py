#!/usr/bin/env python3
"""Enumerate a classic Mac resource fork: MENU, STR#, DITL, WIND, CNTL, PPob.

Written to read MOTU PCI Audio Setup.rsrc, whose i386 binary can no longer run
but whose UI definition is entirely intact.
"""
import sys, struct

ENC = "mac-roman"


def load(path):
    d = open(path, "rb").read()
    dataOff, mapOff, dataLen, mapLen = struct.unpack(">4I", d[:16])
    m = d[mapOff:mapOff + mapLen]
    typeListOff, nameListOff = struct.unpack(">2H", m[24:28])
    numTypes = struct.unpack(">H", m[typeListOff:typeListOff + 2])[0] + 1

    out = {}
    for i in range(numTypes):
        o = typeListOff + 2 + i * 8
        typ, cnt, refOff = struct.unpack(">4sHH", m[o:o + 8])
        typ = typ.decode(ENC)
        items = []
        for j in range(cnt + 1):
            r = typeListOff + refOff + j * 12
            rid, nameOff, attrDataOff = struct.unpack(">hHI", m[r:r + 8])
            dOff = attrDataOff & 0x00FFFFFF
            name = ""
            if nameOff != 0xFFFF:
                p = nameListOff + nameOff
                ln = m[p]
                name = m[p + 1:p + 1 + ln].decode(ENC, "replace")
            base = dataOff + dOff
            ln = struct.unpack(">I", d[base:base + 4])[0]
            items.append((rid, name, d[base + 4:base + 4 + ln]))
        out[typ] = items
    return out


def pstr(b, o):
    n = b[o]
    return b[o + 1:o + 1 + n].decode(ENC, "replace"), o + 1 + n


def menu(b):
    mid, width, height, procID = struct.unpack(">4h", b[:8])
    o = 10 + 4  # filler + enableFlags
    title, o = pstr(b, o)
    items = []
    while o < len(b) and b[o] != 0:
        text, o = pstr(b, o)
        icon, key, mark, style = b[o], b[o + 1], b[o + 2], b[o + 3]
        o += 4
        items.append((text, chr(key) if key > 32 else ""))
    return mid, title, items


def strn(b):
    n = struct.unpack(">H", b[:2])[0]
    o, out = 2, []
    for _ in range(n):
        if o >= len(b):
            break
        s, o = pstr(b, o)
        out.append(s)
    return out


def ditl(b):
    n = struct.unpack(">h", b[:2])[0] + 1
    o, out = 2, []
    for _ in range(n):
        if o + 13 > len(b):
            break
        o += 4                                   # reserved handle
        t, l, bt, r = struct.unpack(">4h", b[o:o + 8]); o += 8
        kind = b[o]; o += 1
        if kind & 0x80:                          # disabled flag
            kind &= 0x7F
        names = {0: "userItem", 1: "Button", 2: "CheckBox", 3: "RadioButton",
                 4: "Control", 5: "StaticText", 7: "EditText", 8: "StaticText",
                 16: "EditText", 32: "Icon", 64: "Picture"}
        if kind in (1, 2, 3, 5, 7, 8, 16):
            s, o = pstr(b, o)
        elif kind in (0, 4, 32, 64):
            ln = b[o]; o += 1 + ln
            s = ""
        else:
            ln = b[o]; o += 1 + ln
            s = ""
        if o % 2:
            o += 1
        out.append((names.get(kind, f"kind{kind}"), s, (l, t, r, bt)))
    return out


res = load(sys.argv[1])

print("=" * 70)
print("MENUS")
print("=" * 70)
for rid, name, b in sorted(res.get("MENU", [])):
    try:
        mid, title, items = menu(b)
    except Exception as ex:
        print(f"  MENU {rid}: parse failed ({ex})")
        continue
    print(f"\n  MENU {rid}  \"{title}\"")
    for text, key in items:
        if text == "-":
            print("      ----")
        else:
            print(f"      {text}{'   (cmd-' + key + ')' if key else ''}")

print()
print("=" * 70)
print("STRING LISTS")
print("=" * 70)
for rid, name, b in sorted(res.get("STR#", [])):
    ss = strn(b)
    print(f"\n  STR# {rid}{'  ' + name if name else ''}   ({len(ss)} strings)")
    for i, s in enumerate(ss, 1):
        if s.strip():
            print(f"      {i:3d}. {s}")

print()
print("=" * 70)
print("DIALOGS / ALERTS (DITL item lists)")
print("=" * 70)
for rid, name, b in sorted(res.get("DITL", [])):
    print(f"\n  DITL {rid}{'  ' + name if name else ''}")
    try:
        for kind, s, rect in ditl(b):
            print(f"      {kind:12s} {s}")
    except Exception as ex:
        print(f"      parse failed ({ex})")

for typ in ("WIND", "DLOG", "ALRT", "CNTL"):
    if typ in res:
        print(f"\n  {typ}: " + ", ".join(
            f"{rid}{'(' + name + ')' if name else ''}" for rid, name, _ in sorted(res[typ])))
