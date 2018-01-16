#!/usr/bin/python2.7

import os
import sys

print "Hello from {platform}!".format(platform=sys.platform)

for pkg in sys.argv[1:]:
  print 'Contents of', pkg
  for root,dir,files in os.walk(pkg):
    for file in files:
      print os.path.join(root, file)
