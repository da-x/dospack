#!/usr/bin/env python2

import sys, os

f =  open(".depend", "w")
f.close()

args = sys.argv[1:]
d = args.index("--")
includes = " ".join(["%s" % (i, ) for i in  args[:d]])
for fname in args[d+1:]:
    oname = os.path.splitext(fname)[0] + ".o"
    cmd = "cpp -MG -MT %s -M %s %s > .depend.tmp" % (oname, fname, includes)
    print cmd
    os.system(cmd)

    f = open(".depend", "a+")
    d = open(".depend.tmp").read()
    d = d.replace(" \\\n", " ")
    for line in d.splitlines():
        _oname, p = line.split(": ", 1)
        deps = []
        for dep in p.split(' '):
            if not dep:
                continue;
            if not os.path.exists(dep):
                for cfileline in open(fname).readlines():
                    cfileline = cfileline.strip()
                    if cfileline.startswith('#include "%s"' % (dep, )):
                        dep = os.path.join(os.path.dirname(fname), dep)
                        break

            deps.append(dep)
        print >>f, "%s: %s" % (_oname, ' '.join(deps))
    f.close()
    os.unlink(".depend.tmp")
