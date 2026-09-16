import idb, sys, struct
with idb.from_file('/u/aap/lib/pure3d/scarface_pc/Scarface.idb') as db:
    api = idb.IDAPython(db)
    segs = list(api.idautils.Segments())
    print("segments:", [(hex(s), api.idc.SegName(s), hex(api.idc.SegEnd(s))) for s in segs], file=sys.stderr)
    with open('segments.txt','w') as sf:
        for s in segs:
            e = api.idc.SegEnd(s); name = api.idc.SegName(s)
            sf.write('%08x %08x %s\n' % (s, e, name))
            try:
                b = api.idc.GetManyBytes(s, e - s)
                if b is None: 
                    # fallback chunked
                    out = bytearray()
                    for a in range(s, e, 0x1000):
                        c = api.idc.GetManyBytes(a, min(0x1000, e-a)) or b'\0'*min(0x1000, e-a)
                        out += c
                    b = bytes(out)
            except Exception as ex:
                print("seg", name, "err", ex, file=sys.stderr); continue
            open('seg_%s_%08x.bin' % (name.strip('.'), s), 'wb').write(b)
            print(name, hex(s), hex(e), len(b), file=sys.stderr)
    # function list and comments
    n=0; c=0
    with open('idb_funcs.txt','w') as ff, open('idb_cmts.txt','w') as cf:
        for f in api.idautils.Functions():
            n+=1
            ff.write('%08x %08x %s\n' % (f, api.idc.FindFuncEnd(f), api.idc.GetFunctionName(f)))
            for rep in (0,1):
                cm = api.idc.GetFunctionCmt(f, rep)
                if cm: c+=1; cf.write('func %08x %s: %s\n' % (f, api.idc.GetFunctionName(f), cm.replace('\n','\\n')))
    print("funcs", n, "func comments", c, file=sys.stderr)
