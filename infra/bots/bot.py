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
  append('skcms/build/clang', 'cc  = {}/bin/clang  '.format(clang_linux))
  append('skcms/build/clang', 'cxx = {}/bin/clang++'.format(clang_linux))
  # TODO(mtklein): once the NDK is new enough (r18?) we can use its Clang again
  append('skcms/build/android.fp16',
         'cc  = {}/bin/clang   -target $target'.format(clang_linux))
  append('skcms/build/android.fp16',
         'cxx = {}/bin/clang++ -target $target'.format(clang_linux))

  call('{ninja}/ninja -C skcms -k 0'.format(ninja=ninja))

else:  # Windows
  win_toolchain = os.path.realpath(sys.argv[2]) + '\\depot_tools\\win_toolchain\\vs_files\\5454e45bf3764c03d3fc1024b3bf5bc41e3ab62c'
  os.environ['PATH'] = win_toolchain + '\\VC\\Tools\\MSVC\\14.14.26428\\bin\\HostX64\\x64;' + os.environ['PATH']
  os.environ['INCLUDE'] = win_toolchain + '\\VC\\Tools\\MSVC\\14.14.26428\\include;'
  os.environ['INCLUDE'] += win_toolchain + '\\win_sdk\\Include\\10.0.17134.0\\shared;'
  os.environ['INCLUDE'] += win_toolchain + '\\win_sdk\\Include\\10.0.17134.0\\ucrt;'
  os.environ['INCLUDE'] += win_toolchain + '\\win_sdk\\Include\\10.0.17134.0\\um;'
  os.environ['LIB'] = win_toolchain + '\\VC\\Tools\\MSVC\\14.14.26428\\lib\\x64;'
  os.environ['LIB'] += win_toolchain + '\\win_sdk\\Lib\\10.0.17134.0\\um\\x64;'
  os.environ['LIB'] += win_toolchain + '\\win_sdk\\Lib\\10.0.17134.0\\ucrt\\x64;'

  call('{ninja}\\ninja.exe -C skcms -f msvs.ninja -k 0'.format(ninja=ninja))
