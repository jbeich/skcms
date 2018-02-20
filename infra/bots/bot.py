#!/usr/bin/python2.7

import os
import subprocess
import sys

ninja = sys.argv[1]

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

  call('{ninja}/ninja -C skcms -k 0'.format(ninja=ninja))

elif 'linux' in sys.platform:
  # Point to clang in our clang_linux package.
  clang_linux = os.path.realpath(sys.argv[3])
  append('skcms/build/clang', 'cc = {}/bin/clang'.format(clang_linux))

  call('{ninja}/ninja -C skcms -k 0'.format(ninja=ninja))

else:  # Windows
  win_toolchain = os.path.realpath(sys.argv[2]) + '\\depot_tools\\win_toolchain\\vs_files\\a9e1098bba66d2acccc377d5ee81265910f29272'
  os.environ['PATH'] = win_toolchain + '\\VC\\Tools\\MSVC\\14.11.25503\\bin\\HostX64\\x64;' + os.environ['PATH']
  os.environ['INCLUDE'] = win_toolchain + '\\VC\\Tools\\MSVC\\14.11.25503\\include;'
  os.environ['INCLUDE'] += win_toolchain + '\\win_sdk\\Include\\10.0.15063.0\\ucrt;'
  os.environ['INCLUDE'] += win_toolchain + '\\win_sdk\\Include\\10.0.15063.0\\um;'
  os.environ['LIB'] = win_toolchain + '\\VC\\Tools\\MSVC\\14.11.25503\\lib\\x64;'
  os.environ['LIB'] += win_toolchain + '\\win_sdk\\Lib\\10.0.15063.0\\um\\x64;'
  os.environ['LIB'] += win_toolchain + '\\win_sdk\\Lib\\10.0.15063.0\\ucrt\\x64;'

  call('{ninja}\\ninja.exe -C skcms -f build\\msvs -k 0'.format(ninja=ninja))
