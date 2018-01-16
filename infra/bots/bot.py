#!/usr/bin/python2.7

import os
import subprocess
import sys

def call(cmd):
  subprocess.check_call(cmd, shell=True)

for pkg in sys.argv[1:]:
  print 'Contents of', pkg
  for root,dir,files in os.walk(pkg):
    for file in files:
      print os.path.join(root, file)

# TODO: temporary until I can figure out where our Ninja is...
if not os.path.isdir('ninja'):
  call('git clone git://github.com/ninja-build/ninja')
call('cd ninja; git fetch')
call('cd ninja; git checkout v1.8.2')
call('cd ninja; python configure.py --bootstrap')

# TODO: and where is my ccache?
if not os.path.isdir('ccache'):
  call('git clone git://github.com/ccache/ccache')
call('cd ccache; git fetch')
call('cd ccache; git checkout v3.3.5')
call('cd ccache; ./autogen.sh')
call('cd ccache; ./configure')
call('cd ccache; make -j')

print "Hello from {platform} in {cwd}!".format(platform=sys.platform,
                                               cwd=os.getcwd())

if 'darwin' in sys.platform:
  # Most Mac systems don't have GCC installed.  Disable those builds.
  with open('skcms/build/gcc', 'a') as f:
    print >>f, 'disabled = true'

  # Ninja will run the remaining (Clang) builds.
  call('env PATH=ninja:ccache:$PATH ninja -C skcms')
