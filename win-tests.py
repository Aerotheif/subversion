"""Driver for running the tests on Windows.

Usage: python win-tests.py [option]
    -r, --release      test the Release configuration
    -d, --debug        test the Debug configuration (default)
    -u URL, --url=URL  run DAV tests against URL
    -v, --verbose      talk more
"""


tests = ['subversion/tests/libsvn_subr/config-test.exe',
         'subversion/tests/libsvn_subr/hashdump-test.exe',
         'subversion/tests/libsvn_subr/stringtest.exe',
         'subversion/tests/libsvn_subr/path-test.exe',
         'subversion/tests/libsvn_subr/stream-test.exe',
         'subversion/tests/libsvn_subr/time-test.exe',
         'subversion/tests/libsvn_wc/translate-test.exe',
         'subversion/tests/libsvn_delta/diff-diff3-test.exe',
         'subversion/tests/libsvn_delta/random-test.exe',
         'subversion/tests/libsvn_subr/target-test.py']

fs_tests = ['subversion/tests/libsvn_fs/run-fs-tests.py',
            'subversion/tests/libsvn_repos/run-repos-tests.py']

client_tests = ['subversion/tests/clients/cmdline/getopt_tests.py',
                'subversion/tests/clients/cmdline/basic_tests.py',
                'subversion/tests/clients/cmdline/commit_tests.py',
                'subversion/tests/clients/cmdline/update_tests.py',
                'subversion/tests/clients/cmdline/switch_tests.py',
                'subversion/tests/clients/cmdline/prop_tests.py',
                'subversion/tests/clients/cmdline/schedule_tests.py',
                'subversion/tests/clients/cmdline/log_tests.py',
                'subversion/tests/clients/cmdline/copy_tests.py',
                'subversion/tests/clients/cmdline/diff_tests.py',
                'subversion/tests/clients/cmdline/externals_tests.py',
                'subversion/tests/clients/cmdline/merge_tests.py',
                'subversion/tests/clients/cmdline/stat_tests.py',
                'subversion/tests/clients/cmdline/trans_tests.py',
                'subversion/tests/clients/cmdline/svnadmin_tests.py',
                'subversion/tests/clients/cmdline/svnlook_tests.py',
                'subversion/tests/clients/cmdline/svnversion_tests.py']


import os, sys, string, shutil, traceback
import getopt

opts, args = getopt.getopt(sys.argv[1:], 'rdvcu:',
                           ['release', 'debug', 'verbose', 'cleanup', 'url='])
if len(args) > 1:
  print 'Warning: non-option arguments after the first one will be ignored'

# Interpret the options and set parameters
all_tests = tests + fs_tests + client_tests
repo_loc = 'local repository.'
base_url = None
verbose = 0
cleanup = None
filter = 'Debug'
log = 'tests.log'

for opt,arg in opts:
  if opt in ['-r', '--release']:
    filter = 'Release'
  elif opt in ['-d', '--debug']:
    filter = 'Debug'
  elif opt in ['-v', '--verbose']:
    verbose = 1
  elif opt in ['-c', '--cleanup']:
    cleanup = 1
  elif opt in ['-u', '--url']:
    all_tests = client_tests
    repo_loc = 'remote repository ' + arg + '.'
    base_url = arg
    log = "dav-tests.log"

print 'Testing', filter, 'configuration on', repo_loc

# Calculate the source and test directory names
abs_srcdir = os.path.abspath("")
if len(args) == 0:
  abs_builddir = abs_srcdir
  remove_execs = 1
  create_dirs = 0
else:
  abs_builddir = os.path.abspath(args[0])
  remove_execs = 0
  create_dirs = 1


# Have to move the executables where the tests expect them to be
copied_execs = []   # Store copied exec files to avoid the final dir scan

def create_target_dir(dirname):
  if create_dirs:
    tgt_dir = os.path.join(abs_builddir, dirname)
    if not os.path.exists(tgt_dir):
      if verbose:
        print "mkdir:", tgt_dir
      os.makedirs(tgt_dir)

def copy_execs(filter, dirname, names):
  global copied_execs
  if os.path.basename(dirname) != filter:
    return
  for name in names:
    if os.path.splitext(name)[1] != ".exe":
      continue
    src = os.path.join(dirname, name)
    dir = os.path.dirname(dirname)
    tgt = os.path.join(abs_builddir, dir, name)
    create_target_dir(dir)
    try:
      if verbose:
        print "copy:", src
        print "  to:", tgt
      shutil.copy(src, tgt)
      copied_execs.append(tgt)
    except:
      traceback.print_exc(file=sys.stdout)
      pass
os.path.walk("subversion", copy_execs, filter)
create_target_dir('subversion/tests/clients/cmdline')

# Run the tests
sys.path.insert(0, os.path.join(abs_srcdir, 'build'))
import run_tests
th = run_tests.TestHarness(abs_srcdir, abs_builddir, sys.executable, None,
                           os.path.join(abs_builddir, log),
                           base_url, 1, cleanup)
old_cwd = os.getcwd()
try:
  os.chdir(abs_builddir)
  failed = th.run(all_tests)
except:
  os.chdir(old_cwd)
  raise
else:
  os.chdir(old_cwd)


# Remove the execs again
if remove_execs:
  for tgt in copied_execs:
    try:
      if os.path.isfile(tgt):
        if verbose:
          print "kill:", tgt
        os.unlink(tgt)
    except:
      traceback.print_exc(file=sys.stdout)
      pass


# Print final status
if failed:
  print
  print 'FAIL:', sys.argv[0]
  sys.exit(1)
