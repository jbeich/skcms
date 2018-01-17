#!/usr/bin/python2.7

import os
import subprocess
import sys

def call(cmd):
  subprocess.check_call(cmd, shell=True)

# TODO: temporary until I can figure out where our Ninja is...
if not os.path.isdir('ninja'):
  call('git clone git://github.com/ninja-build/ninja')
call('cd ninja; git fetch')
call('cd ninja; git checkout v1.8.2')
call('cd ninja; python configure.py --bootstrap')

print "Hello from {platform} in {cwd}!".format(platform=sys.platform,
                                               cwd=os.getcwd())

if 'darwin' in sys.platform:
  # Our Mac bots don't have ccache or GCC installed.
  with open('skcms/build/clang', 'a') as f:
    print >>f, 'cc = clang'
  with open('skcms/build/gcc', 'a') as f:
    print >>f, 'disabled = true'

  # Our Mac bot toolchains are too old for LSAN.
  with open('skcms/build/clang.lsan', 'a') as f:
    print >>f, 'disabled = true'

if 'linux' in sys.platform:
  # Point to clang in our clang_linux package.
  clang_linux = os.path.realpath(sys.argv[1])
  with open('skcms/build/clang', 'a') as f:
    print >>f, 'cc = {clang_linux}/bin/clang'.format(clang_linux=clang_linux)

call('ninja/ninja -C skcms -k 0')
