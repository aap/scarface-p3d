import idb, sys
with idb.from_file('/u/aap/lib/pure3d/scarface_pc/Scarface.idb') as db:
    api = idb.IDAPython(db)
    names = list(api.idautils.Names())
    print("names:", len(names), file=sys.stderr)
    with open('idb_names.txt','w') as f:
        for ea, n in names:
            f.write("%08x %s\n" % (ea, n))
    # structs
    try:
        n = api.idc.GetFirstStrucIdx()
        cnt=0
        with open('idb_structs.txt','w') as f:
            while n != -1 and n != 0xffffffff:
                sid = api.idc.GetStrucId(n)
                f.write("struct %s size=%d\n" % (api.idc.GetStrucName(sid), api.idc.GetStrucSize(sid)))
                off = api.idc.GetFirstMember(sid)
                while off != -1 and off != 0xffffffff:
                    mn = api.idc.GetMemberName(sid, off)
                    if mn: f.write("  %04x %s (%d)\n" % (off, mn, api.idc.GetMemberSize(sid, off)))
                    off = api.idc.GetStrucNextOff(sid, off)
                n = api.idc.GetNextStrucIdx(n); cnt+=1
        print("structs:", cnt, file=sys.stderr)
    except Exception as e:
        print("struct err", e, file=sys.stderr)
