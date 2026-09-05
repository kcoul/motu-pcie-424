import subprocess, struct, sys, re
B="/Library/Extensions/MOTUPCIAudio.kext/Contents/PlugIns/HALPlugin.bundle/Contents/MacOS/HALPlugin"
# section map: vmaddr -> fileoff
secs=[]
out=subprocess.run(["otool","-arch","x86_64","-l",B],capture_output=True,text=True).stdout
cur={}
for line in out.splitlines():
    line=line.strip()
    m=re.match(r'^sectname (\S+)',line)
    if m: cur={'sect':m.group(1)}
    m=re.match(r'^segname (\S+)',line)
    if m: cur['seg']=m.group(1)
    m=re.match(r'^addr 0x([0-9a-f]+)',line)
    if m: cur['addr']=int(m.group(1),16)
    m=re.match(r'^size 0x([0-9a-f]+)',line)
    if m: cur['size']=int(m.group(1),16)
    m=re.match(r'^offset (\d+)',line)
    if m:
        cur['off']=int(m.group(1))
        if 'addr' in cur: secs.append(dict(cur))
def a2o(a):
    for s in secs:
        if s['addr']<=a<s['addr']+s['size']: return s['off']+(a-s['addr'])
    return None
# symbol map
syms={}
nm=subprocess.run(["nm","-arch","x86_64","-n",B],capture_output=True,text=True).stdout
raw=[]
for line in nm.splitlines():
    p=line.split(None,2)
    if len(p)==3 and p[0].strip():
        try: raw.append((int(p[0],16),p[2]))
        except: pass
for a,n in raw: syms.setdefault(a,n)
def dem(n):
    return subprocess.run(["c++filt",n],capture_output=True,text=True).stdout.strip()
data=open(B,'rb').read()
# find fat offset for x86_64 slice
if data[:4] in (b'\xca\xfe\xba\xbe',):
    nfat=struct.unpack(">I",data[4:8])[0]
    for i in range(nfat):
        e=8+i*20
        cpu,sub,off,size,align=struct.unpack(">5I",data[e:e+20])
        if cpu==0x01000007: base=off; break
else: base=0
targets=[n for a,n in raw if n.startswith("__ZTV")]
want=sys.argv[1:] if len(sys.argv)>1 else ["CueMixAPIImpl","AudioWireCardImpl","SMPTEAPIImpl","TalkbackAPIImpl","AudioWireInterfaceImpl"]
for t in targets:
    d=dem(t)
    if not any(w in d for w in want): continue
    addr=[a for a,n in raw if n==t][0]
    off=a2o(addr)
    if off is None: continue
    print(f"\n=== {d}  (vmaddr 0x{addr:x}) ===")
    for slot in range(0,40):
        p=base+off+16+slot*8
        val=struct.unpack("<Q",data[p:p+8])[0]
        if val==0: break
        nmv=syms.get(val)
        if nmv is None: 
            cands=[a for a,_ in raw if a<=val]
            if not cands: break
            near=max(cands); nmv=syms.get(near,"?")+f"+0x{val-near:x}"
        print(f"  [{slot:2d}] {dem(nmv) if nmv.startswith('__Z') else nmv}")
