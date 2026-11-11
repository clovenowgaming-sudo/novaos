#!/usr/bin/env python3
import sys, os
src, dst, apps = sys.argv[1], sys.argv[2], sys.argv[3:]
t = open(src).read()
lines = "\n    ".join("module2 /apps/%s app" % os.path.basename(a) for a in apps)
open(dst, "w").write(t.replace("#APPS#", lines))
