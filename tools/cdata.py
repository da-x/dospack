#!/usr/bin/env python

import sys, os

def main():
    inputf, outputf = sys.argv[1:3]
    f = open(outputf, "w")
    try:
        for i, c in enumerate(open(inputf).read()):
            print >>f, "0x%x," % ord(c),
            if i % 16 == 0:
                print >>f, ""
        f.close()
    except:
        os.unlink(outputf)
        raise

main()
