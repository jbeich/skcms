#!/usr/bin/python2.7

import os
import subprocess
import sys
import tempfile

build_or_test = sys.argv[1]
assert build_or_test in ["build", "test"]

local_or_rbe = sys.argv[2]
assert local_or_rbe in ["local", "rbe"]

def call(cmd):
  print "Executing: " + cmd
  subprocess.check_call(cmd, shell=True)

def bazel(args):
  cmd = ["bazel", "--output_user_root=" + bazel_cache_dir] + args
  call(" ".join(cmd))

print "Hello from {platform} in {cwd}!".format(platform=sys.platform,
                                               cwd=os.getcwd())

# # Print debug information.
# print "Environment:"
# for k, v in os.environ.items():
#   print k + ": " + v
# call("pwd")
# call("ls -l")
# call("find")
# call("mount")
# call("df -h")

# Create a temporary directory for the Bazel cache.
#
# We cannot use the default Bazel cache location ($HOME/.cache/bazel) because:
#
#  - The cache can be large (>10G).
#  - Swarming bots have limited storage space on the root partition (15G).
#  - Because the above, the Bazel build fails with a "no space left on
#    device" error.
#  - The Bazel cache under $HOME/.cache/bazel lingers after the tryjob
#    completes, causing the Swarming bot to be quarantined due to low disk
#    space.
#  - Generally, it's considered poor hygiene to leave a bot in a different
#    state.
#
# The temporary directory created by the below function call lives under
# /mnt/pd0, which has significantly more storage space, and will be wiped
# after the tryjob completes.
#
# Reference: https://docs.bazel.build/versions/master/output_directories.html#current-layout.
bazel_cache_dir = tempfile.mkdtemp(prefix="bazel-cache-",
                                   dir=os.environ["TMPDIR"])

# Print the Bazel version.
bazel(["version"])

# Run the requested Bazel command.
os.chdir("skcms")
cmd = [build_or_test, "//..."]
if local_or_rbe == "rbe":
  cmd.append("--config=linux-rbe")
bazel(cmd)
