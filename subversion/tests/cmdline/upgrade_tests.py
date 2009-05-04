#!/usr/bin/env python
#
#  upgrade_tests.py:  test the working copy upgrade process
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2009 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

#
# These tests exercise the upgrade capabilities of 'svn cleanup' as it
# moves working copies between wc-1 and wc-ng.
#

import os, sys, tarfile, shutil

import svntest

Item = svntest.wc.StateItem
XFail = svntest.testcase.XFail
SkipUnless = svntest.testcase.SkipUnless

def replace_sbox_with_tarfile(sbox, tar_filename):
  try:
    svntest.main.safe_rmtree(sbox.wc_dir)
  except OSError as e:
    pass

  tarpath = os.path.join(os.path.dirname(sys.argv[0]), 'upgrade_tests_data',
                         tar_filename)
  t = tarfile.open(tarpath, 'r:bz2')
  for member in t.getmembers():
    t.extract(member, svntest.main.temp_dir)

  shutil.move(os.path.join(svntest.main.temp_dir, tar_filename.split('.')[0]),
              sbox.wc_dir)


def check_format(sbox, expected_format):
  import sqlite3

  for root, dirs, files in os.walk(sbox.wc_dir):
    db = sqlite3.connect(os.path.join(root, '.svn', 'wc.db'))
    c = db.cursor()
    c.execute('pragma user_version;')
    found_format = c.fetchone()[0]
    db.close()

    if found_format != expected_format:
      raise svntest.Failure("found format '%d'; expected '%d'; in wc '%s'" %
                            (found_format, expected_format, root))

    if '.svn' in dirs:
      dirs.remove('.svn')


def basic_upgrade(sbox):
  "basic upgrade behavior"

  replace_sbox_with_tarfile(sbox, 'basic_upgrade.tar.bz2')

  # Attempt to use the working copy, this should give an error
  expected_stderr = (".*Working copy format is too old; run 'svn cleanup' "
                     "to upgrade")
  svntest.actions.run_and_verify_svn(None, None, expected_stderr,
                                     'info', sbox.wc_dir)


  # Now upgrade the working copy
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cleanup', sbox.wc_dir)

  # Actually check the sanity of the upgraded working copy
  check_format(sbox, 12)


def upgrade_1_5(sbox):
  "test upgrading from a 1.5-era working copy"

  replace_sbox_with_tarfile(sbox, 'upgrade_1_5.tar.bz2')

  # Attempt to use the working copy, this should give an error
  expected_stderr = (".*Working copy format is too old; run 'svn cleanup' "
                     "to upgrade")
  svntest.actions.run_and_verify_svn(None, None, expected_stderr,
                                     'info', sbox.wc_dir)


  # Now upgrade the working copy
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cleanup', sbox.wc_dir)

  # Check the format of the working copy
  check_format(sbox, 12)


def logs_left_1_5(sbox):
  "test upgrading from a 1.5-era wc with stale logs"

  replace_sbox_with_tarfile(sbox, 'logs_left_1_5.tar.bz2')

  # Try to upgrade, this should give an error
  expected_stderr = (".*Cannot upgrade with existing logs; please "
                     "run 'svn cleanup' with Subversion 1.6")
  svntest.actions.run_and_verify_svn(None, None, expected_stderr,
                                     'cleanup', sbox.wc_dir)


def upgrade_wcprops(sbox):
  "test upgrading a working copy with wcprops"

  replace_sbox_with_tarfile(sbox, 'upgrade_wcprops.tar.bz2')
  svntest.actions.run_and_verify_svn(None, None, [],
                                     'cleanup', sbox.wc_dir)

  # Make sure that .svn/all-wcprops has disappeared
  if os.path.exists(os.path.join(sbox.wc_dir, '.svn', 'all-wcprops')):
    raise svntest.Failure("all-wcprops file still exists")


########################################################################
# Run the tests

def has_sqlite():
  try:
    import sqlite3
    return True
  except ImportError:
    return False

# list all tests here, starting with None:
test_list = [ None,
              SkipUnless(basic_upgrade, has_sqlite),
              SkipUnless(upgrade_1_5, has_sqlite),
              logs_left_1_5,
              upgrade_wcprops,
             ]


if __name__ == '__main__':
  svntest.main.run_tests(test_list)
  # NOTREACHED
