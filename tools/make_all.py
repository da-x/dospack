#!/usr/bin/env python

import sys, os

f = open(sys.argv[1], "w")
try:
    print >>f, "#define DP_VIDEO_ENGINE dp_video_gl_init"
    for arg in sys.argv[2:]:
        if not arg.startswith('desktop/'):
            print >>f, "#include \"%s\"" % (arg, )
    f.close()
except:
    os.unlink(sys.argv[1])
    raise


