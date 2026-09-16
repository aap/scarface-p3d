#!/usr/bin/env python3
"""imagemap.py -- per 32K bucket: attributed bytes, unattributed bytes, dominant class"""
import os, sys, pickle, collections
D=os.path.dirname(os.path.abspath(__file__))
C=pickle.load(open(D+'/.cov_cache.pkl','rb'))
attr=C['attr']; funcs=C['funcs']
fend={s:e for s,e,_ in funcs}
STEP=0x8000
b=collections.defaultdict(lambda: [0,0,collections.Counter()])
for s,e,_ in funcs:
    k=s & ~(STEP-1)
    b[k][0]+=e-s
    if s in attr: b[k][2][attr[s][0]]+=e-s
    else: b[k][1]+=e-s
for k in sorted(b):
    tot,un,cc=b[k]
    top=', '.join('%s(%d)'%(c,n) for c,n in cc.most_common(3))
    print('%08x tot%7d unattr%7d (%3d%%)  %s'%(k,tot,un,100*un//max(tot,1),top))
