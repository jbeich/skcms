#!/usr/bin/python2.7

import os
import subprocess
import sys

def call(cmd):
  subprocess.check_call(cmd, shell=True)

def append(path, line):
  with open(path, 'a') as f:
    print >>f, line

print "Hello from {platform} in {cwd}!".format(platform=sys.platform,
                                               cwd=os.getcwd())

if 'darwin' in sys.platform:
  # Our Mac bots don't have a real GCC installed.
  append('skcms/build/gcc', 'disabled = true')

  # Our Mac bot toolchains are too old for LSAN.
  append('skcms/build/clang.lsan', 'disabled = true')

if 'linux' in sys.platform:
  # Point to clang in our clang_linux package.
  clang_linux = os.path.realpath(sys.argv[3])
  append('skcms/build/clang', 'cc = {}/bin/clang'.format(clang_linux))

  # We're seeing an unexpected error when running gcc.tsan/tests:
  # FATAL: ThreadSanitizer: unexpected memory mapping 0x55e16e36c000-0x55e16e374000
  append('skcms/build/gcc.tsan', 'disabled = true')

call('{ninja}/ninja -C skcms -k 0'.format(ninja=sys.argv[1]))
