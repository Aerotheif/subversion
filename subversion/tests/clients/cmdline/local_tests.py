#!/usr/bin/env python
#
#  local_tests.py:  testing working-copy interactions with ra_local
#
#  Subversion is a tool for revision control.
#  See http://subversion.tigris.org for more information.
#
# ====================================================================
# Copyright (c) 2000-2001 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

import shutil, string, sys, os.path

# Load the svn testing framework package:
import svntest
from svntest import *


######################################################################
# Globals

# Where we want all the repositories and working copies to live.
# Each test will have its own!
general_repo_dir = "repositories"
general_wc_dir = "working_copies"


# temp directory in which we will create our 'pristine' local
# repository and other scratch data
temp_dir = 'local_tmp'


# derivatives of the tmp dir.
pristine_dir = os.path.join(temp_dir, "repos")
greek_dump_dir = os.path.join(temp_dir, "greekfiles")


######################################################################
# Utilities shared by these tests


# Used by every test, so that they can run independently of one
# another.  The first time it's run, it runs 'svnadmin' to create a
# repository and then 'svn imports' a greek tree.  Thereafter, it just
# recursively copies the repos.

def guarantee_greek_repository(path):
  """Guarantee that a local svn repository exists at PATH, containing
  nothing but the greek-tree at revision 1."""

  if path == pristine_dir:
    print "ERROR:  attempt to overwrite the pristine repos!  Aborting."
    exit(1)

  # If there's no pristine repos, create one.
  if not os.path.exists(pristine_dir):
    svntest.main.create_repos(pristine_dir)

    # dump the greek tree to disk.
    svntest.main.write_tree(greek_dump_dir,
                             [[x[0], x[1]] for x in svntest.main.greek_tree])

    # figger out the "file:" url needed to run import
    url = "file://" + os.path.abspath(pristine_dir)
    # import the greek tree.
    output = svntest.main.run_svn("import", url, greek_dump_dir)

    # verify the printed output of 'svn import'.
    lastline = string.strip(output.pop())
    if lastline != 'Commit succeeded.':
      print "ERROR:  import did not 'succeed', while creating greek repos."
      print "The final line from 'svn import' was:"
      print lastline
      exit(1)
    output_tree = svntest.tree.build_tree_from_commit(output)

    output_list = []
    path_list = [x[0] for x in svntest.main.greek_tree]
    for apath in path_list:
      item = [ os.path.join(".", apath), None, {'verb' : 'Adding'}]
      output_list.append(item)
    expected_output_tree = svntest.tree.build_generic_tree(output_list)

    if svntest.tree.compare_trees(output_tree, expected_output_tree):
      print "ERROR:  output of import command is unexpected."
      exit(1)

  # Now that the pristine repos exists, copy it to PATH.
  if os.path.exists(path):
    shutil.rmtree(path)
  if not os.path.exists(os.path.dirname(path)):
    os.makedirs(os.path.dirname(path))
  shutil.copytree(pristine_dir, path)


# For the functions below, the OUTPUT_TREE and DISK_TREE args need to
# be created by feeding carefully constructed lists to
# svntest.tree.build_generic_tree().

def run_and_verify_checkout(URL, wc_dir_name, output_tree, disk_tree,
                            singleton_handler_a=None,
                            singleton_handler_b=None):
  """Checkout the the URL into a new directory WC_DIR_NAME.

  The subcommand output will be verified against OUTPUT_TREE,
  and the working copy itself will be verified against DISK_TREE.
  SINGLETON_HANDLER_A and SINGLETON_HANDLER_B will be passed to
  svntest.tree.compare_trees - see that function's doc string for more details.
  Return 0 if successful."""

  # Remove dir if it's already there.
  svntest.main.remove_wc(wc_dir_name)

  # Checkout and make a tree of the output.
  output = svntest.main.run_svn ('co', URL, '-d', wc_dir_name)
  mytree = svntest.tree.build_tree_from_checkout (output)

  # Verify actual output against expected output.
  if svntest.tree.compare_trees (mytree, output_tree):
    return 1

  # Create a tree by scanning the working copy
  mytree = svntest.tree.build_tree_from_wc (wc_dir_name)

  # Verify expected disk against actual disk.
  if svntest.tree.compare_trees (mytree, disk_tree,
                             singleton_handler_a, singleton_handler_b):
    return 1

  return 0


def run_and_verify_update(wc_dir_name,
                          output_tree, disk_tree, status_tree,
                          singleton_handler_a=None,
                          singleton_handler_b=None,
                          *args):
  """Update WC_DIR_NAME into a new directory WC_DIR_NAME.  *ARGS are
  any extra optional args to the update subcommand.

  The subcommand output will be verified against OUTPUT_TREE, and the
  working copy itself will be verified against DISK_TREE.  If optional
  STATUS_OUTPUT_TREE is given, then 'svn status' output will be
  compared.  (This is a good way to check that revision numbers were
  bumped.)  SINGLETON_HANDLER_A and SINGLETON_HANDLER_B will be passed to
  svntest.tree.compare_trees - see that function's doc string for more details.
  Return 0 if successful."""

  # Update and make a tree of the output.
  output = svntest.main.run_svn ('up', wc_dir_name, *args)
  mytree = svntest.tree.build_tree_from_checkout (output)

  # Verify actual output against expected output.
  if svntest.tree.compare_trees (mytree, output_tree):
    return 1

  # Create a tree by scanning the working copy
  mytree = svntest.tree.build_tree_from_wc (wc_dir_name)

  # Verify expected disk against actual disk.
  if svntest.tree.compare_trees (mytree, disk_tree,
                             singleton_handler_a, singleton_handler_b):
    return 1

  # Verify via 'status' command too, if possible.
  if status_tree:
    if run_and_verify_status(wc_dir_name, status_tree):
      return 1

  return 0


def run_and_verify_commit(wc_dir_name, output_tree, status_output_tree,
                          singleton_handler_a=None,
                          singleton_handler_b=None,
                          *args):
  """Commit and verify results within working copy WC_DIR_NAME,
  sending ARGS to the commit subcommand.

  The subcommand output will be verified against OUTPUT_TREE.  If
  optional STATUS_OUTPUT_TREE is given, then 'svn status' output will
  be compared.  (This is a good way to check that revision numbers
  were bumped.)  SINGLETON_HANDLER_A and SINGLETON_HANDLER_B will be passed to
  svntest.tree.compare_trees - see that function's doc string for more details.
  Return 0 if successful."""

  # Commit.
  output = svntest.main.run_svn ('ci', *args)

  # Remove the final output line, and verify that 'Commit succeeded'.
  lastline = ""
  if len(output):
    lastline = string.strip(output.pop())

  if lastline != 'Commit succeeded.':
    print "ERROR:  commit did not 'succeed'."
    print "The final line from 'svn ci' was:"
    print lastline
    return 1

  # Convert the output into a tree.
  mytree = svntest.tree.build_tree_from_commit (output)

  # Verify actual output against expected output.
  if svntest.tree.compare_trees (mytree, output_tree):
    return 1

  # Verify via 'status' command too, if possible.
  if status_output_tree:
    if run_and_verify_status(wc_dir_name, status_output_tree):
      return 1

  return 0


def run_and_verify_status(wc_dir_name, output_tree,
                          singleton_handler_a=None,
                          singleton_handler_b=None):
  """Run 'status' on WC_DIR_NAME and compare it with the
  expected OUTPUT_TREE.  SINGLETON_HANDLER_A and SINGLETON_HANDLER_B will
  be passed to svntest.tree.compare_trees - see that function's doc string for
  more details.
  Return 0 on success."""

  output = svntest.main.run_svn ('status', wc_dir_name)

  mytree = svntest.tree.build_tree_from_status (output)

  # Verify actual output against expected output.
  if svntest.tree.compare_trees (mytree, output_tree,
                             singleton_handler_a, singleton_handler_b):
    return 1

  return 0



##################################
# Meta-helpers for tests. :)


# A way for a test to bootstrap.
def make_repo_and_wc(test_name):
  """Create a fresh repository and checkout a wc from it.

  The repo and wc directories will both be named TEST_NAME, and
  repsectively live within the global dirs 'general_repo_dir' and
  'general_wc_dir' (variables defined at the top of this test
  suite.)  Return 0 on success, non-zero on failure."""

  # Where the repos and wc for this test should be created.
  wc_dir = os.path.join(general_wc_dir, test_name)
  repo_dir = os.path.join(general_repo_dir, test_name)

  # Create (or copy afresh) a new repos with a greek tree in it.
  guarantee_greek_repository(repo_dir)

  # Generate the expected output tree.
  output_list = []
  path_list = [x[0] for x in svntest.main.greek_tree]
  for path in path_list:
    item = [ os.path.join(wc_dir, path), None, {'status' : 'A '} ]
    output_list.append(item)
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Generate an expected wc tree.
  expected_wc_tree = svntest.tree.build_generic_tree(svntest.main.greek_tree)

  # Do a checkout, and verify the resulting output and disk contents.
  url = 'file:///' + os.path.abspath(repo_dir)
  return run_and_verify_checkout(url, wc_dir,
                                 expected_output_tree,
                                 expected_wc_tree)


# Duplicate a working copy or other dir.
def duplicate_dir(wc_name, wc_copy_name):
  """Copy the working copy WC_NAME to WC_COPY_NAME.  Overwrite any
  existing tree at that location."""

  if os.path.exists(wc_copy_name):
    shutil.rmtree(wc_copy_name)
  shutil.copytree(wc_name, wc_copy_name)



# A generic starting state for the output of 'svn status'.
# Returns a list of the form:
#
#   [ ['repo', None, {'status':'_ ', 'wc_rev':'1', 'repos_rev':'1'}],
#     ['repo/A', None, {'status':'_ ', 'wc_rev':'1', 'repos_rev':'1'}],
#     ['repo/A/mu', None, {'status':'_ ', 'wc_rev':'1', 'repos_rev':'1'}],
#     ... ]
#
def get_virginal_status_list(wc_dir, rev):
  """Given a WC_DIR, return a list describing the expected 'status'
  output of an up-to-date working copy at revision REV.  (i.e. the
  repository and working copy files are all at REV).

  NOTE:  REV is a string, not an integer. :)

  The list returned is suitable for passing to
  svntest.tree.build_generic_tree()."""

  output_list = [[wc_dir, None,
                  {'status' : '_ ',
                   'wc_rev' : rev,
                   'repos_rev' : rev}]]
  path_list = [x[0] for x in svntest.main.greek_tree]
  for path in path_list:
    item = [os.path.join(wc_dir, path), None,
            {'status' : '_ ',
             'wc_rev' : rev,
             'repos_rev' : rev}]
    output_list.append(item)

  return output_list

######################################################################
# Tests
#
#   Each test must return 0 on success or non-zero on failure.

#----------------------------------------------------------------------

def basic_checkout():
  "basic checkout of a wc"

  return make_repo_and_wc('basic-checkout')

#----------------------------------------------------------------------

def basic_status():
  "basic status command"

  wc_dir = os.path.join (general_wc_dir, 'basic-status')

  if make_repo_and_wc('basic-status'):
    return 1

  # Created expected output tree for 'svn status'
  status_list = get_virginal_status_list(wc_dir, '1')
  expected_output_tree = svntest.tree.build_generic_tree(status_list)

  return run_and_verify_status (wc_dir, expected_output_tree)

#----------------------------------------------------------------------

def basic_commit():
  "commit '.' in working copy"

  wc_dir = os.path.join (general_wc_dir, 'basic_commit')

  if make_repo_and_wc('basic_commit'):
    return 1

  # Make a couple of local mods to files
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.main.file_append (mu_path, 'appended mu text')
  svntest.main.file_append (rho_path, 'new appended text for rho')

  # Created expected output tree for 'svn ci'
  output_list = [ [mu_path, None, {'verb' : 'Changing' }],
                  [rho_path, None, {'verb' : 'Changing' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 2.
  status_list = get_virginal_status_list(wc_dir, '2')
  for item in status_list:
    if (item[0] != mu_path) and (item[0] != rho_path):
      item[2]['wc_rev'] = '1'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  return run_and_verify_commit (wc_dir,
                                expected_output_tree,
                                expected_status_tree,
                                None,
                                None,
                                wc_dir)

#----------------------------------------------------------------------

def commit_one_file():
  "commit one file only"

  wc_dir = os.path.join (general_wc_dir, 'commit_one_file')

  if make_repo_and_wc('commit_one_file'):
    return 1

  # Make a couple of local mods to files
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.main.file_append (mu_path, 'appended mu text')
  svntest.main.file_append (rho_path, 'new appended text for rho')

  # Created expected output tree for 'svn ci';  we're only committing rho.
  output_list = [ [rho_path, None, {'verb' : 'Changing' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected status tree; all local revisions should be at 1,
  # but rho should be at revision 2.
  status_list = get_virginal_status_list(wc_dir, '2')
  for item in status_list:
    if (item[0] != rho_path):
      item[2]['wc_rev'] = '1'
    # And mu should still be locally modified
    if (item[0] == mu_path):
      item[2]['status'] = 'M '
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  return run_and_verify_commit (wc_dir,
                                expected_output_tree,
                                expected_status_tree,
                                None,
                                None,
                                rho_path)

#----------------------------------------------------------------------

def commit_multiple_targets():
  "commit multiple targets"

  wc_dir = os.path.join (general_wc_dir, 'commit_multiple_targets')

  if make_repo_and_wc('commit_multiple_targets'):
    return 1

  # This test will commit three targets:  psi, B, and pi.  In that order.

  # Make local mods to many files.
  AB_path = os.path.join(wc_dir, 'A', 'B')
  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  pi_path = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  omega_path = os.path.join(wc_dir, 'A', 'D', 'H', 'omega')
  psi_path = os.path.join(wc_dir, 'A', 'D', 'H', 'psi')
  svntest.main.file_append (lambda_path, 'new appended text for lambda')
  svntest.main.file_append (rho_path, 'new appended text for rho')
  svntest.main.file_append (pi_path, 'new appended text for pi')
  svntest.main.file_append (omega_path, 'new appended text for omega')
  svntest.main.file_append (psi_path, 'new appended text for psi')

  # Just for kicks, add a property to A/D/G as well.  We'll make sure
  # that it *doesn't* get committed.
  ADG_path = os.path.join(wc_dir, 'A', 'D', 'G')
  svntest.main.run_svn('propset', 'foo', 'bar', ADG_path)

  # Created expected output tree for 'svn ci'.  We should see changes
  # only on these three targets, no others.
  output_list = [ [psi_path, None, {'verb' : 'Changing' }],
                  [lambda_path, None, {'verb' : 'Changing' }],
                  [pi_path, None, {'verb' : 'Changing' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected status tree; all local revisions should be at 1,
  # but our three targets should be at 2.
  status_list = get_virginal_status_list(wc_dir, '2')
  for item in status_list:
    if ((item[0] != psi_path) and (item[0] != lambda_path)
        and (item[0] != pi_path)):
      item[2]['wc_rev'] = '1'
    # rho and omega should still display as locally modified:
    if ((item[0] == rho_path) or (item[0] == omega_path)):
      item[2]['status'] = 'M '
    # A/D/G should still have a local property set, too.
    if (item[0] == ADG_path):
      item[2]['status'] = '_M'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  return run_and_verify_commit (wc_dir,
                                expected_output_tree,
                                expected_status_tree,
                                None,
                                None,
                                psi_path, AB_path, pi_path)

#----------------------------------------------------------------------


def commit_multiple_targets_2():
  "commit multiple targets, 2nd variation"

  wc_dir = os.path.join (general_wc_dir, 'commit_multiple_targets_2')

  if make_repo_and_wc('commit_multiple_targets_2'):
    return 1

  # This test will commit three targets:  psi, B, omega and pi.  In that order.

  # Make local mods to many files.
  AB_path = os.path.join(wc_dir, 'A', 'B')
  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  pi_path = os.path.join(wc_dir, 'A', 'D', 'G', 'pi')
  omega_path = os.path.join(wc_dir, 'A', 'D', 'H', 'omega')
  psi_path = os.path.join(wc_dir, 'A', 'D', 'H', 'psi')
  svntest.main.file_append (lambda_path, 'new appended text for lambda')
  svntest.main.file_append (rho_path, 'new appended text for rho')
  svntest.main.file_append (pi_path, 'new appended text for pi')
  svntest.main.file_append (omega_path, 'new appended text for omega')
  svntest.main.file_append (psi_path, 'new appended text for psi')

  # Just for kicks, add a property to A/D/G as well.  We'll make sure
  # that it *doesn't* get committed.
  ADG_path = os.path.join(wc_dir, 'A', 'D', 'G')
  svntest.main.run_svn('propset', 'foo', 'bar', ADG_path)

  # Created expected output tree for 'svn ci'.  We should see changes
  # only on these three targets, no others.
  output_list = [ [psi_path, None, {'verb' : 'Changing' }],
                  [lambda_path, None, {'verb' : 'Changing' }],
                  [omega_path, None, {'verb' : 'Changing' }],
                  [pi_path, None, {'verb' : 'Changing' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected status tree; all local revisions should be at 1,
  # but our four targets should be at 2.
  status_list = get_virginal_status_list(wc_dir, '2')
  for item in status_list:
    if ((item[0] != psi_path) and (item[0] != lambda_path)
        and (item[0] != pi_path) and (item[0] != omega_path)):
      item[2]['wc_rev'] = '1'
    # rho should still display as locally modified:
    if (item[0] == rho_path):
      item[2]['status'] = 'M '
    # A/D/G should still have a local property set, too.
    if (item[0] == ADG_path):
      item[2]['status'] = '_M'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  return run_and_verify_commit (wc_dir,
                                expected_output_tree,
                                expected_status_tree,
                                None,
                                None,
                                psi_path, AB_path, omega_path, pi_path)

#----------------------------------------------------------------------

def basic_update():
  "update '.' in working copy"

  wc_dir = os.path.join (general_wc_dir, 'basic_update')

  if make_repo_and_wc('basic_update'):
    return 1

  # Make a backup copy of the working copy
  wc_backup = os.path.join (general_wc_dir, 'basic_update_backup')
  duplicate_dir(wc_dir, wc_backup)

  # Make a couple of local mods to files
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.main.file_append (mu_path, 'appended mu text')
  svntest.main.file_append (rho_path, 'new appended text for rho')

  # Created expected output tree for 'svn ci'
  output_list = [ [mu_path, None, {'verb' : 'Changing' }],
                  [rho_path, None, {'verb' : 'Changing' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 2.
  status_list = get_virginal_status_list(wc_dir, '2')
  for item in status_list:
    if (item[0] != mu_path) and (item[0] != rho_path):
      item[2]['wc_rev'] = '1'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Commit.
  if run_and_verify_commit (wc_dir, expected_output_tree,
                            expected_status_tree, None, None, wc_dir):
    return 1

  # Create expected output tree for an update of the wc_backup.
  output_list = [[os.path.join(wc_backup, 'A', 'mu'),
                  None, {'status' : 'U '}],
                 [os.path.join(wc_backup, 'A', 'D', 'G', 'rho'),
                   None, {'status' : 'U '}]]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected disk tree for the update.
  my_greek_tree = svntest.main.copy_greek_tree()
  my_greek_tree[2][1] = my_greek_tree[2][1] + 'appended mu text'
  my_greek_tree[14][1] = my_greek_tree[14][1] + 'new appended text for rho'
  expected_disk_tree = svntest.tree.build_generic_tree(my_greek_tree)

  # Create expected status tree for the update.
  status_list = get_virginal_status_list(wc_backup, '2')
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Do the update and check the results in three ways.
  return run_and_verify_update(wc_backup,
                               expected_output_tree,
                               expected_disk_tree,
                               expected_status_tree)

#----------------------------------------------------------------------
def basic_merge():
  "merge into working copy"

  wc_dir = os.path.join (general_wc_dir, 'basic_merge')

  if make_repo_and_wc('basic_merge'):
    return 1
  # First change the greek tree to make two files 10 lines long
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  mu_text = ""
  rho_text = ""
  for x in range(2,11):
    mu_text = mu_text + '\nThis is line ' + `x` + ' in mu'
    rho_text = rho_text + '\nThis is line ' + `x` + ' in rho'
  svntest.main.file_append (mu_path, mu_text)
  svntest.main.file_append (rho_path, rho_text)

  # Create expected output tree for initial commit
  output_list = [ [mu_path, None, {'verb' : 'Changing' }],
                  [rho_path, None, {'verb' : 'Changing' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected status tree : rev 2 for rho and mu.
  status_list = get_virginal_status_list(wc_dir, '1')
  for item in status_list:
    item[2]['repos_rev'] = '2'
    if (item[0] == mu_path) or (item[0] == rho_path):
      item[2]['wc_rev'] = '2'
      item[2]['status'] = '_ '
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Initial commit.
  if run_and_verify_commit (wc_dir, expected_output_tree,
                            expected_status_tree, None, None, wc_dir):
    return 1
  # Make a backup copy of the working copy
  wc_backup = os.path.join (general_wc_dir, 'basic_merge_backup')
  duplicate_dir(wc_dir, wc_backup)

  # Make a couple of local mods to files
  svntest.main.file_append (mu_path, ' Appended to line 10 of mu')
  svntest.main.file_append (rho_path, ' Appended to line 10 of rho')

  # Created expected output tree for 'svn ci'
  output_list = [ [mu_path, None, {'verb' : 'Changing' }],
                  [rho_path, None, {'verb' : 'Changing' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 3.
  status_list = get_virginal_status_list(wc_dir, '1')
  for item in status_list:
    item[2]['repos_rev'] = '3'
    if (item[0] == mu_path) or (item[0] == rho_path):
      item[2]['wc_rev'] = '3'
      item[2]['status'] = '_ '
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Commit.
  if run_and_verify_commit (wc_dir, expected_output_tree,
                            expected_status_tree, None, None, wc_dir):
    return 1

  # Make local mods to wc_backup by recreating mu and rho
  mu_path_backup = os.path.join(wc_backup, 'A', 'mu')
  rho_path_backup = os.path.join(wc_backup, 'A', 'D', 'G', 'rho')
  fp_mu = open(mu_path_backup, 'w+')
  # open in 'truncate to zero then write" mode
  backup_mu_text='This is the new line 1 in the backup copy of mu'
  for x in range(2,11):
    backup_mu_text = backup_mu_text + '\nThis is line ' + `x` + ' in mu'
  fp_mu.write(backup_mu_text)
  fp_mu.close()

  fp_rho = open(rho_path_backup, 'w+') # now open rho in write mode
  backup_rho_text='This is the new line 1 in the backup copy of rho'
  for x in range(2,11):
    backup_rho_text = backup_rho_text + '\nThis is line ' + `x` + ' in rho'
  fp_rho.write(backup_rho_text)
  fp_rho.close()

  # Create expected output tree for an update of the wc_backup.
  output_list = [[os.path.join(wc_backup, 'A', 'mu'),
                  None, {'status' : 'G '}],
                 [os.path.join(wc_backup, 'A', 'D', 'G', 'rho'),
                  None, {'status' : 'G '}]]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected disk tree for the update.
  my_greek_tree = svntest.main.copy_greek_tree()
  my_greek_tree[2][1] = 'This is the new line 1 in the backup copy of mu'
  for x in range(2,11):
    my_greek_tree[2][1] = my_greek_tree[2][1] + '\nThis is line ' + `x` + ' in mu'
  my_greek_tree[2][1] = my_greek_tree[2][1] + ' Appended to line 10 of mu'
  my_greek_tree[14][1] = 'This is the new line 1 in the backup copy of rho'
  for x in range(2,11):
    my_greek_tree[14][1] = my_greek_tree[14][1] + '\nThis is line ' + `x` + ' in rho'
  my_greek_tree[14][1] = my_greek_tree[14][1] + ' Appended to line 10 of rho'
  expected_disk_tree = svntest.tree.build_generic_tree(my_greek_tree)

  # Create expected status tree for the update.
  status_list = get_virginal_status_list(wc_backup, '3')
  for item in status_list:
    if (item[0] == mu_path_backup) or (item[0] == rho_path_backup):
      item[2]['status'] = 'M '
  # Some discrepancy here about whether this should be M or G...M for now.
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Do the update and check the results in three ways.
  return run_and_verify_update(wc_backup,
                               expected_output_tree,
                               expected_disk_tree,
                               expected_status_tree)


#----------------------------------------------------------------------

# Ok, so extra_files is a global dictionary...but we have to have a list of
# expected extra files that the backup copy will have after a conflict that
# both conflict_from_wc_top() and verify_rej_file() can see.

extra_files = { 'mu.rej':'mu reject file',
                'rho.rej':'rho reject file'}

def verify_rej_file(node):
  "Handles a reject file from a conflict."
  # four files are expected - mu~, mu reject file, rho~ and rho reject file
  length_of_name = len(node.name)
  if ((node.name[0:3] == "mu.") and
      (node.name[(length_of_name - 4):length_of_name] == ".rej")):
    del extra_files['mu.rej']
  elif ((node.name[0:4] == "rho.") and
      (node.name[(length_of_name - 4):length_of_name] == ".rej")):
    del extra_files['rho.rej']
  elif (node.name == "mu~"):
    # Some versions of PATCH produce these twiddle files, but not all.
    # Thus, these two cases for mu~ and rho~ have been added to make the
    # test more robust.
    return 0
  elif (node.name == "rho~"):
    return 0
  else:
    print node.name,"is an unknown file."
    raise svntest.tree.SVNTreeUnequal


def basic_conflict():
  "make a conflict in working copy"

  wc_dir = os.path.join (general_wc_dir, 'basic_conflict')

  if make_repo_and_wc('basic_conflict'):
    return 1

  # Make a backup copy of the working copy
  wc_backup = os.path.join (general_wc_dir, 'basic_conflict_backup')
  duplicate_dir(wc_dir, wc_backup)

  # Make a couple of local mods to files which will be committed
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  rho_path = os.path.join(wc_dir, 'A', 'D', 'G', 'rho')
  svntest.main.file_append (mu_path, '\nOriginal appended text for mu')
  svntest.main.file_append (rho_path, '\nOriginal appended text for rho')

  # Make a couple of local mods to files which will be conflicted
  mu_path_backup = os.path.join(wc_backup, 'A', 'mu')
  rho_path_backup = os.path.join(wc_backup, 'A', 'D', 'G', 'rho')
  svntest.main.file_append (mu_path_backup,
                             '\nConflicting appended text for mu')
  svntest.main.file_append (rho_path_backup,
                             '\nConflicting appended text for rho')

  # Created expected output tree for 'svn ci'
  output_list = [ [mu_path, None, {'verb' : 'Changing' }],
                  [rho_path, None, {'verb' : 'Changing' }] ]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected status tree; all local revisions should be at 1,
  # but mu and rho should be at revision 2.
  status_list = get_virginal_status_list(wc_dir, '2')
  for item in status_list:
    if (item[0] != mu_path) and (item[0] != rho_path):
      item[2]['wc_rev'] = '1'
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Commit.
  if run_and_verify_commit (wc_dir, expected_output_tree,
                            expected_status_tree, None, None, wc_dir):
    return 1

  # Create expected output tree for an update of the wc_backup.
  output_list = [[os.path.join(wc_backup, 'A', 'mu'),
                  None, {'status' : 'C '}],
                 [os.path.join(wc_backup, 'A', 'D', 'G', 'rho'),
                   None, {'status' : 'C '}]]
  expected_output_tree = svntest.tree.build_generic_tree(output_list)

  # Create expected disk tree for the update.
  my_greek_tree = svntest.main.copy_greek_tree()
  my_greek_tree[2][1] = my_greek_tree[2][1] + '\nConflicting appended text for mu'
  my_greek_tree[14][1] = my_greek_tree[14][1] + '\nConflicting appended text for rho'
  expected_disk_tree = svntest.tree.build_generic_tree(my_greek_tree)

  # Create expected status tree for the update.
  status_list = get_virginal_status_list(wc_backup, '2')
  for item in status_list:
    if (item[0] == mu_path_backup) or (item[0] == rho_path_backup):
      item[2]['status'] = 'C '
  expected_status_tree = svntest.tree.build_generic_tree(status_list)

  # Do the update and check the results in three ways.
  if run_and_verify_update(wc_backup,
                           expected_output_tree,
                           expected_disk_tree,
                           expected_status_tree,
                           verify_rej_file,
                           None):
    return 1
  # verify that the extra_files list is now empty.
  if len(extra_files) != 0:
    # Because we want to be a well-behaved test, we silently return
    # non-zero if the test fails.  However, these two print statements
    # would probably reveal the cause for the failure, if they were
    # uncommented:
    #
    # print "Not all extra reject files have been accounted for:"
    # print extra_files
    return 1

  return 0

########################################################################
## List all tests here, starting with None:
test_list = [ None,
              basic_checkout,
              basic_status,
              basic_commit,
              commit_one_file,
              commit_multiple_targets,
              commit_multiple_targets_2,
              basic_update,
              basic_merge,
              basic_conflict
             ]

if __name__ == '__main__':
  ## And run the main test routine on them:
  err = svntest.main.run_tests(test_list)
  ## Remove all scratchwork: the 'pristine' repository, greek tree, etc.
  ## This ensures that an 'import' will happen the next time we run.
  if os.path.exists(temp_dir):
    shutil.rmtree(temp_dir)
  ## Return whatever main() returned to the OS.
  sys.exit(err)

### End of file.
# local variables:
# eval: (load-file "../../../svn-dev.el")
# end:
