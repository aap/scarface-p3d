import idb, re, sys
def san(n): return re.sub(r'[^A-Za-z0-9_.:]', '_', n)
with idb.from_file('/u/aap/lib/pure3d/scarface_pc/Scarface.idb') as db:
    api = idb.IDAPython(db)
    fc=0; ic=0
    funcs = {f: api.idc.FindFuncEnd(f) for f in api.idautils.Functions()}
    with open('idb_cmts.txt','w') as cf:
        for f,e in funcs.items():
            for rep in (False, True):
                try: cm = api.ida_funcs.get_func_cmt(f, rep)
                except Exception: cm=None
                if cm: fc+=1; cf.write('func %08x %s: %s\n' % (f, api.idc.GetFunctionName(f), cm.replace('\n','\\n')))
        # inline comments: iterate heads in all functions (slow-ish); use api.idautils.Heads
        for f,e in funcs.items():
            try:
                for h in api.idautils.Heads(f, e):
                    for rep in (0,1):
                        cm = api.idc.GetCommentEx(h, rep)
                        if cm: ic+=1; cf.write('%08x: %s\n' % (h, cm.replace('\n','\\n')))
            except Exception as ex:
                pass
    print('func cmts', fc, 'inline cmts', ic, file=sys.stderr)
with open('idb_names.txt') as nf, open('r2flags.r2','w') as rf:
    rf.write('e asm.bits=32\ne asm.arch=x86\n')
    for l in nf:
        a,n = l.split(None,1); n=n.strip()
        if re.match(r'(sub_|loc_|off_|unk_|byte_|dword_|word_|nullsub|j_|flt_|dbl_|stru_|xmmword_|def_|jpt_)', n): continue
        rf.write('f sym.%s 1 @ 0x%s\n' % (san(n), a))
