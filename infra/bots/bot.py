#!/usr/bin/python2.7

import os
import subprocess
import sys

print "Hello from {platform}!".format(platform=sys.platform)

for pkg in sys.argv[1:]:
  print 'Contents of', pkg
  for root,dir,files in os.walk(pkg):
    for file in files:
      print os.path.join(root, file)

if 'darwin' in sys.platform:
  # Most Mac systems don't have GCC installed.  Disable those builds.
  subprocess.check_call(['echo', 'disabled = true', '>>build/gcc'])
  # Ninja will run the remaining Clang builds.
  subprocess.check_call(['ninja', '-v'])
