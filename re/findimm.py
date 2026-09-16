import pefile, struct, sys
pe = pefile.PE('/u/aap/lib/pure3d/scarface_pc/Scarface_orig.exe')
data = open('/u/aap/lib/pure3d/scarface_pc/Scarface_orig.exe','rb').read()
base = pe.OPTIONAL_HEADER.ImageBase
def off2va(o):
    for s in pe.sections:
        if s.PointerToRawData <= o < s.PointerToRawData + s.SizeOfRawData:
            return base + s.VirtualAddress + (o - s.PointerToRawData)
    return None
for a in sys.argv[1:]:
    v = int(a,16); pat = struct.pack('<I', v); i = 0; hits=[]
    while True:
        i = data.find(pat, i)
        if i < 0: break
        hits.append(off2va(i)); i += 1
    print('%08x: %s' % (v, ' '.join('%08x'%h for h in hits if h)))
