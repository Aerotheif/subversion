#  -*- coding: utf-8 -*-
#  See http://subversion.apache.org for more information.
#    Licensed to the Apache Software Foundation (ASF) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The ASF licenses this file
#    to you under the Apache License, Version 2.0 (the
#    "License"); you may not use this file except in compliance
#    with the License.  You may obtain a copy of the License at
#      http://www.apache.org/licenses/LICENSE-2.0
#    Unless required by applicable law or agreed to in writing,
#    software distributed under the License is distributed on an
#    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#    KIND, either express or implied.  See the License for the
#    specific language governing permissions and limitations
#    under the License.
from svntest import err
Skip = svntest.testcase.Skip_deco
SkipUnless = svntest.testcase.SkipUnless_deco
XFail = svntest.testcase.XFail_deco
Issues = svntest.testcase.Issues_deco
Issue = svntest.testcase.Issue_deco
Wimp = svntest.testcase.Wimp_deco
def make_git_diff_header(target_path, repos_relpath,
                         old_tag, new_tag, add=False, src_label=None,
                         dst_label=None, delete=False, text_changes=True,
                         cp=False, mv=False, copyfrom_path=None):
  """ Generate the expected 'git diff' header for file TARGET_PATH.
  REPOS_RELPATH is the location of the path relative to the repository root.
  The old and new versions ("revision X", or "working copy") must be
  specified in OLD_TAG and NEW_TAG.
  SRC_LABEL and DST_LABEL are paths or urls that are added to the diff
  labels if we're diffing against the repository. ADD, DELETE, CP and MV
  denotes the operations performed on the file. COPYFROM_PATH is the source
  of a copy or move.  Return the header as an array of newline-terminated
  strings."""

  path_as_shown = target_path.replace('\\', '/')
  if src_label:
    src_label = src_label.replace('\\', '/')
    src_label = '\t(.../' + src_label + ')'
  else:
    src_label = ''
  if dst_label:
    dst_label = dst_label.replace('\\', '/')
    dst_label = '\t(.../' + dst_label + ')'
  else:
    dst_label = ''

  if add:
    output = [
      "Index: " + path_as_shown + "\n",
      "===================================================================\n",
      "diff --git a/" + repos_relpath + " b/" + repos_relpath + "\n",
      "new file mode 10644\n",
    ]
    if text_changes:
      output.extend([
        "--- /dev/null\t(" + old_tag + ")\n",
        "+++ b/" + repos_relpath + dst_label + "\t(" + new_tag + ")\n"
      ])
  elif delete:
    output = [
      "Index: " + path_as_shown + "\n",
      "===================================================================\n",
      "diff --git a/" + repos_relpath + " b/" + repos_relpath + "\n",
      "deleted file mode 10644\n",
    ]
    if text_changes:
      output.extend([
        "--- a/" + repos_relpath + src_label + "\t(" + old_tag + ")\n",
        "+++ /dev/null\t(" + new_tag + ")\n"
      ])
  elif cp:
    output = [
      "Index: " + path_as_shown + "\n",
      "===================================================================\n",
      "diff --git a/" + copyfrom_path + " b/" + repos_relpath + "\n",
      "copy from " + copyfrom_path + "\n",
      "copy to " + repos_relpath + "\n",
    ]
    if text_changes:
      output.extend([
        "--- a/" + copyfrom_path + src_label + "\t(" + old_tag + ")\n",
        "+++ b/" + repos_relpath + "\t(" + new_tag + ")\n"
      ])
  elif mv:
    return [
      "Index: " + path_as_shown + "\n",
      "===================================================================\n",
      "diff --git a/" + copyfrom_path + " b/" + path_as_shown + "\n",
      "rename from " + copyfrom_path + "\n",
      "rename to " + repos_relpath + "\n",
    ]
    if text_changes:
      output.extend([
        "--- a/" + copyfrom_path + src_label + "\t(" + old_tag + ")\n",
        "+++ b/" + repos_relpath + "\t(" + new_tag + ")\n"
      ])
  else:
    output = [
      "Index: " + path_as_shown + "\n",
      "===================================================================\n",
      "diff --git a/" + repos_relpath + " b/" + repos_relpath + "\n",
      "--- a/" + repos_relpath + src_label + "\t(" + old_tag + ")\n",
      "+++ b/" + repos_relpath + dst_label + "\t(" + new_tag + ")\n",
    ]
  return output


  repo_diff(wc_dir, 1, 4, check_add_a_file_in_a_subdir_reverse)
    "Index: iota\n",
    "===================================================================\n",
    "--- iota\t(revision 1)\n",
    "+++ iota\t(revision 2)\n",
    "## -0,0 +1 ##\n",
    "+native\n" ]
  expected_reverse_output[2] = expected_reverse_output[2].replace("1", "2")
  expected_reverse_output[3] = expected_reverse_output[3].replace("2", "1")
  expected_reverse_output[7] = expected_reverse_output[7].replace("Added",
  expected_reverse_output[8] = "## -1 +0,0 ##\n"
  expected_reverse_output[9] = "-native\n"
  expected_rev1_output = list(expected_output)
  expected_rev1_output[3] = expected_rev1_output[3].replace("revision 2",
                                                            "working copy")
  svntest.actions.run_and_verify_svn(None, expected_rev1_output, [],
  svntest.actions.run_and_verify_svn(None, expected_rev1_output, [],
@Issue(1019)
  theta_contents = open(os.path.join(sys.path[0], "theta.bin"), 'rb').read()
@Issue(977)
@Issue(891)
  regex = 'svn: E195012: Unable to find repository location for \'.*\''
  regex = 'svn: E160013: \'.*\' was not found in the repository'
@Issue(1311)
  "diff between two file URLs"
  verify_expected_output(out, "+pvalue")
  verify_expected_output(out, "+pvalue")  # fails at r7481
  verify_expected_output(out, "+pvalue")
@XFail()
  # Repos->WC diff of the file showing copies as adds
  exit_code, diff_output, err_output = svntest.main.run_svn(
                                         None, 'diff', '-r', '1',
                                         '--show-copies-as-adds', pi2_path)
  if check_diff_output(diff_output,
                       pi2_path,
                       'A') :
    raise svntest.Failure

  # Repos->WC of the containing directory
  # Repos->WC of the containing directory showing copies as adds
  exit_code, diff_output, err_output = svntest.main.run_svn(
    None, 'diff', '-r', '1', '--show-copies-as-adds', os.path.join('A', 'D'))

  if check_diff_output(diff_output,
                       pi_path,
                       'D') :
    raise svntest.Failure

  if check_diff_output(diff_output,
                       pi2_path,
                       'A') :
    raise svntest.Failure

  # WC->WC of the file showing copies as adds
  exit_code, diff_output, err_output = svntest.main.run_svn(
                                         None, 'diff',
                                         '--show-copies-as-adds', pi2_path)
  if check_diff_output(diff_output,
                       pi2_path,
                       'A') :
    raise svntest.Failure

  # Repos->WC diff of file after the rename
  # Repos->WC diff of file after the rename. The local file is not
  # a copy anymore (it has schedule "normal"), so --show-copies-as-adds
  # should have no effect.
  exit_code, diff_output, err_output = svntest.main.run_svn(
                                         None, 'diff', '-r', '1',
                                         '--show-copies-as-adds', pi2_path)
  if check_diff_output(diff_output,
                       pi2_path,
                       'M') :
    raise svntest.Failure

  # Repos->repos diff after the rename
  ### --show-copies-as-adds has no effect
  verify_expected_output(diff_output, "-v")
@Issue(2333)
    None, 'diff', '--show-copies-as-adds', os.path.join('A', 'D'))
  # Commit
  # repos->repos with explicit URL arg
  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            '-r', '1:2',
                                                            '^/A')
  if check_diff_output(diff_output,
                       os.path.join('D', 'G', 'pi'),
                       'D') :
    raise svntest.Failure
  if check_diff_output(diff_output,
                       os.path.join('D', 'I', 'pi'),
                       'A') :
    raise svntest.Failure

  # Go to the parent of the moved directory
  os.chdir(os.path.join('A','D'))
  # repos->wc diff in the parent
  if check_diff_output(diff_output,
                       os.path.join('G', 'pi'),
                       'D') :
    raise svntest.Failure
  if check_diff_output(diff_output,
                       os.path.join('I', 'pi'),
                       'A') :
    raise svntest.Failure

  # repos->repos diff in the parent
  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            '-r', '1:2')

  if check_diff_output(diff_output,
                       os.path.join('G', 'pi'),
                       'D') :
    raise svntest.Failure
  if check_diff_output(diff_output,
                       os.path.join('I', 'pi'),
                       'A') :
  # Go to the move target directory
  os.chdir('I')

  # repos->wc diff while within the moved directory (should be empty)
  exit_code, diff_output, err_output = svntest.main.run_svn(None, 'diff',
                                                            '-r', '1')
  if diff_output:
    raise svntest.Failure

  # repos->repos diff while within the moved directory (should be empty)
  if diff_output:

  add_diff = [
    "## -0,0 +1 ##\n",
    "+r2value\n",
    "## -0,0 +1 ##\n",
    "+r2value\n"]
  del_diff = [
    "\n",
    "Property changes on: A\n",
    "___________________________________________________________________\n",
    "Deleted: dirprop\n",
    "## -1 +0,0 ##\n",
    "-r2value\n",
    "\n",
    "Property changes on: iota\n",
    "___________________________________________________________________\n",
    "Deleted: fileprop\n",
    "## -1 +0,0 ##\n",
    "-r2value\n"]


  expected_output_r1_r2 = list(make_diff_header('A', 'revision 1', 'revision 2')
                               + add_diff[:6]
                               + make_diff_header('iota', 'revision 1',
                                                   'revision 2')
                               + add_diff[7:])

  expected_output_r2_r1 = list(make_diff_header('A', 'revision 2',
                                                'revision 1')
                               + del_diff[:6]
                               + make_diff_header('iota', 'revision 2',
                                                  'revision 1')
                               + del_diff[7:])

  expected_output_r1 = list(make_diff_header('A', 'revision 1',
                                             'working copy')
                            + add_diff[:6]
                            + make_diff_header('iota', 'revision 1',
                                               'working copy')
                            + add_diff[7:])
  expected_output_base_r1 = list(make_diff_header('A', 'working copy',
                                                  'revision 1')
                                 + del_diff[:6]
                                 + make_diff_header('iota', 'working copy',
                                                    'revision 1')
                                 + del_diff[7:])
  expected = svntest.verify.UnorderedOutput(expected_output_r1)
  expected = svntest.verify.UnorderedOutput(expected_output_base_r1)
  # presence of local mods (with the exception of diff header changes).
  expected = svntest.verify.UnorderedOutput(expected_output_r1)
  expected = svntest.verify.UnorderedOutput(expected_output_base_r1)
                                                "working copy") + [
    "Index: A\n",
    "===================================================================\n",
    "--- A\t(revision 2)\n",
    "+++ A\t(working copy)\n",
    "## -1 +1 ##\n",
    "-r2value\n",
    "+workingvalue\n",
    "## -0,0 +1 ##\n",
    "+newworkingvalue\n",
    "Index: iota\n",
    "===================================================================\n",
    "--- iota\t(revision 2)\n",
    "+++ iota\t(working copy)\n",
    "## -1 +1 ##\n",
    "-r2value\n",
    "+workingvalue\n",
    "## -0,0 +1 ##\n",
    "+newworkingvalue\n" ]
  diff_foo = [
    "## -0,0 +1 ##\n",
    "+propvalue\n",
    ]
  diff_X = [
    "## -0,0 +1 ##\n",
    "+propvalue\n",
    ]
  diff_X_bar = [
    "Property changes on: X/bar\n",
    "## -0,0 +1 ##\n",
    "+propvalue\n",
    ]

  diff_X_r1_base = make_diff_header("X", "revision 1",
                                         "working copy") + diff_X
  diff_X_base_r3 = make_diff_header("X", "working copy",
                                         "revision 3") + diff_X
  diff_foo_r1_base = make_diff_header("foo", "revision 0",
                                             "revision 3") + diff_foo
  diff_foo_base_r3 = make_diff_header("foo", "revision 0",
                                             "revision 3") + diff_foo
  diff_X_bar_r1_base = make_diff_header("X/bar", "revision 0",
                                                 "revision 3") + diff_X_bar
  diff_X_bar_base_r3 = make_diff_header("X/bar", "revision 0",
                                                 "revision 3") + diff_X_bar

  expected_output_r1_base = svntest.verify.UnorderedOutput(diff_X_r1_base +
                                                           diff_X_bar_r1_base +
                                                           diff_foo_r1_base)
  expected_output_base_r3 = svntest.verify.UnorderedOutput(diff_foo_base_r3 +
                                                           diff_X_bar_base_r3 +
                                                           diff_X_base_r3)
  svntest.actions.run_and_verify_svn(None, expected_output_r1_base, [],
  svntest.actions.run_and_verify_svn(None, expected_output_r1_base, [],
  svntest.actions.run_and_verify_svn(None, expected_output_base_r3, [],
  # Check that a base->repos diff with copyfrom shows deleted and added lines.
  p = sbox.ospath
  # Add props to some items that will be deleted, and commit.
  sbox.simple_propset('prop', 'val',
                      'A/C',
                      'A/D/gamma',
                      'A/D/H/chi')
  sbox.simple_commit() # r2
  sbox.simple_update()
  # Content modification.
  svntest.main.file_append(p('A/mu'), 'new text\n')
  # Prop modification.
  sbox.simple_propset('prop', 'val', 'iota')
  # Both content and prop mods.
  svntest.main.file_append(p('A/D/G/tau'), 'new text\n')
  sbox.simple_propset('prop', 'val', 'A/D/G/tau')

  # File addition.
  svntest.main.file_append(p('newfile'), 'new text\n')
  svntest.main.file_append(p('newfile2'), 'new text\n')
  sbox.simple_add('newfile',
                  'newfile2')
  sbox.simple_propset('prop', 'val', 'newfile')

  # File deletion.
  sbox.simple_rm('A/B/lambda',
                 'A/D/gamma')

  # Directory addition.
  os.makedirs(p('P'))
  os.makedirs(p('Q/R'))
  svntest.main.file_append(p('Q/newfile'), 'new text\n')
  svntest.main.file_append(p('Q/R/newfile'), 'new text\n')
  sbox.simple_add('P',
                  'Q')
  sbox.simple_propset('prop', 'val',
                      'P',
                      'Q/newfile')

  # Directory deletion.
  sbox.simple_rm('A/D/H',
                 'A/C')

  # Commit, because diff-summarize handles repos-repos only.
  #svntest.main.run_svn(False, 'st', wc_dir)
  sbox.simple_commit() # r3
  svntest.actions.run_and_verify_diff_summarize(expected_diff,
                                                p('iota'), '-c3')
  svntest.actions.run_and_verify_diff_summarize(expected_diff,
                                                p('iota'), '-c-3')
  # wc-wc diff summary for a directory.
    'A/mu':           Item(status='M '),
    'iota':           Item(status=' M'),
    'A/D/G/tau':      Item(status='MM'),
    'newfile':        Item(status='A '),
    'newfile2':       Item(status='A '),
    'P':              Item(status='A '),
    'Q':              Item(status='A '),
    'Q/newfile':      Item(status='A '),
    'Q/R':            Item(status='A '),
    'Q/R/newfile':    Item(status='A '),
    'A/B/lambda':     Item(status='D '),
    'A/C':            Item(status='D '),
    'A/D/gamma':      Item(status='D '),
    'A/D/H':          Item(status='D '),
    'A/D/H/chi':      Item(status='D '),
    'A/D/H/psi':      Item(status='D '),
    'A/D/H/omega':    Item(status='D '),
    })

  expected_reverse_diff = svntest.wc.State(wc_dir, {
    'A/mu':           Item(status='M '),
    'iota':           Item(status=' M'),
    'A/D/G/tau':      Item(status='MM'),
    'newfile':        Item(status='D '),
    'newfile2':       Item(status='D '),
    'P':              Item(status='D '),
    'Q':              Item(status='D '),
    'Q/newfile':      Item(status='D '),
    'Q/R':            Item(status='D '),
    'Q/R/newfile':    Item(status='D '),
    'A/B/lambda':     Item(status='A '),
    'A/C':            Item(status='A '),
    'A/D/gamma':      Item(status='A '),
    'A/D/H':          Item(status='A '),
    'A/D/H/chi':      Item(status='A '),
    'A/D/H/psi':      Item(status='A '),
    'A/D/H/omega':    Item(status='A '),
  svntest.actions.run_and_verify_diff_summarize(expected_diff,
                                                wc_dir, '-c3')
  svntest.actions.run_and_verify_diff_summarize(expected_reverse_diff,
                                                wc_dir, '-c-3')

#----------------------------------------------------------------------
@Issue(2121)
@Issue(2600)
  ### right now, we cannot denote that kappa is a local-add rather than a
  ### child of the A/D/C copy. thus, it appears in the status output as a
  ### (M)odified child.
  B_path = os.path.join('A', 'B')
    "## -0,0 +1 ##\n",
    "+bar1\n",
    "## -0,0 +1 ##\n",
    "+bar2\n",
    "## -0,0 +1 ##\n",
    "+bar3\n",
    "Property changes on: A/B\n",
    "## -0,0 +1 ##\n",
    "+bar4\n"]

  dot_header = make_diff_header(".", "revision 1", "working copy")
  iota_header = make_diff_header('iota', "revision 1", "working copy")
  A_header = make_diff_header('A', "revision 1", "working copy")
  B_header = make_diff_header(B_path, "revision 1", "working copy")

  expected_empty = svntest.verify.UnorderedOutput(dot_header + diff[:6])
  expected_files = svntest.verify.UnorderedOutput(dot_header + diff[:6]
                                                  + iota_header + diff[7:12])
  expected_immediates = svntest.verify.UnorderedOutput(dot_header + diff[:6]
                                                       + iota_header
                                                       + diff[7:12]
                                                       +  A_header + diff[8:18])
  expected_infinity = svntest.verify.UnorderedOutput(dot_header + diff[:6]
                                                       + iota_header
                                                       + diff[7:12]
                                                       +  A_header + diff[8:18]
                                                       + B_header + diff[12:])
  dot_header = make_diff_header(".", "revision 1", "revision 2")
  iota_header = make_diff_header('iota', "revision 1", "revision 2")
  A_header = make_diff_header('A', "revision 1", "revision 2")
  B_header = make_diff_header(B_path, "revision 1", "revision 2")

  expected_empty = svntest.verify.UnorderedOutput(dot_header + diff[:6])
  expected_files = svntest.verify.UnorderedOutput(dot_header + diff[:6]
                                                  + iota_header + diff[7:12])
  expected_immediates = svntest.verify.UnorderedOutput(dot_header + diff[:6]
                                                       + iota_header
                                                       + diff[7:12]
                                                       +  A_header + diff[8:18])
  expected_infinity = svntest.verify.UnorderedOutput(dot_header + diff[:6]
                                                       + iota_header
                                                       + diff[7:12]
                                                       +  A_header + diff[8:18]
                                                       + B_header + diff[12:])

  # Test repos-repos diff.
    "Index: A/B\n",
    "===================================================================\n",
    "--- A/B\t(revision 2)\n",
    "+++ A/B\t(working copy)\n",
    "Property changes on: A/B\n",
    "Modified: foo4\n",
    "## -1 +1 ##\n",
    "-bar4\n",
    "+baz4\n",
    "Index: A\n",
    "===================================================================\n",
    "--- A\t(revision 2)\n",
    "+++ A\t(working copy)\n",
    "Property changes on: A\n",
    "Modified: foo3\n",
    "## -1 +1 ##\n",
    "-bar3\n",
    "+baz3\n",
    "Index: A/mu\n",
    "===================================================================\n",
    "--- A/mu\t(revision 1)\n",
    "+++ A/mu\t(working copy)\n",
    "@@ -1 +1,2 @@\n",
    " This is the file 'mu'.\n",
    "+new text\n",
    "Property changes on: iota\n",
    "Modified: foo2\n",
    "## -1 +1 ##\n",
    "-bar2\n",
    "+baz2\n",
    "Index: .\n",
    "--- .\t(revision 2)\n",
    "+++ .\t(working copy)\n",
    "\n",
    "Property changes on: .\n",
    "___________________________________________________________________\n",
    "Modified: foo1\n",
    "## -1 +1 ##\n",
    "-bar1\n",
    "+baz1\n" ]

  expected_empty = svntest.verify.UnorderedOutput(diff_wc_repos[43:])
  expected_files = svntest.verify.UnorderedOutput(diff_wc_repos[29:])
  expected_immediates = svntest.verify.UnorderedOutput(diff_wc_repos[11:22]
                                                       +diff_wc_repos[29:])
@Issue(2920)
  time.sleep(1.1)
  svntest.actions.run_and_verify_svn(None, [], err.INVALID_DIFF_OPTION,
    'for arg in sys.argv[1:]:\n  print(arg)\n')
    os.path.abspath(svntest.wc.text_base_path("iota")) + "\n",
    os.path.abspath("iota") + "\n"])
@XFail()
@Issue(3295)
# Diff against old revision of the parent directory of a removed and
# locally re-added file.
@XFail()
@Issue(3797)
  "diff -r1 of dir with removed-then-readded file"
def diff_git_format_wc_wc(sbox):
  "create a diff in git unidiff format for wc-wc"
  sbox.build()
  wc_dir = sbox.wc_dir
  iota_path = os.path.join(wc_dir, 'iota')
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  new_path = os.path.join(wc_dir, 'new')
  lambda_path = os.path.join(wc_dir, 'A', 'B', 'lambda')
  lambda_copied_path = os.path.join(wc_dir, 'A', 'B', 'lambda_copied')
  alpha_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha')
  alpha_copied_path = os.path.join(wc_dir, 'A', 'B', 'E', 'alpha_copied')

  svntest.main.file_append(iota_path, "Changed 'iota'.\n")
  svntest.main.file_append(new_path, "This is the file 'new'.\n")
  svntest.main.run_svn(None, 'add', new_path)
  svntest.main.run_svn(None, 'rm', mu_path)
  svntest.main.run_svn(None, 'cp', lambda_path, lambda_copied_path)
  svntest.main.run_svn(None, 'cp', alpha_path, alpha_copied_path)
  svntest.main.file_append(alpha_copied_path, "This is a copy of 'alpha'.\n")

  ### We're not testing moved paths

  expected_output = make_git_diff_header(lambda_copied_path,
                                         "A/B/lambda_copied",
                                         "revision 1", "working copy",
                                         copyfrom_path="A/B/lambda", cp=True,
                                         text_changes=False) \
  + make_git_diff_header(mu_path, "A/mu", "revision 1",
                                         "working copy",
                                         delete=True) + [
    "@@ -1 +0,0 @@\n",
    "-This is the file 'mu'.\n",
  ] + make_git_diff_header(alpha_copied_path, "A/B/E/alpha_copied",
                         "revision 0", "working copy",
                         copyfrom_path="A/B/E/alpha", cp=True,
                         text_changes=True) + [
    "@@ -1 +1,2 @@\n",
    " This is the file 'alpha'.\n",
    "+This is a copy of 'alpha'.\n",
  ] + make_git_diff_header(new_path, "new", "revision 0",
                           "working copy", add=True) + [
    "@@ -0,0 +1 @@\n",
    "+This is the file 'new'.\n",
  ] +  make_git_diff_header(iota_path, "iota", "revision 1",
                            "working copy") + [
    "@@ -1 +1,2 @@\n",
    " This is the file 'iota'.\n",
    "+Changed 'iota'.\n",
  ]

  expected = svntest.verify.UnorderedOutput(expected_output)

  svntest.actions.run_and_verify_svn(None, expected, [], 'diff',
                                     '--git', wc_dir)

def diff_git_format_url_wc(sbox):
  "create a diff in git unidiff format for url-wc"
  sbox.build()
  wc_dir = sbox.wc_dir
  repo_url = sbox.repo_url
  iota_path = os.path.join(wc_dir, 'iota')
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  new_path = os.path.join(wc_dir, 'new')
  svntest.main.file_append(iota_path, "Changed 'iota'.\n")
  svntest.main.file_append(new_path, "This is the file 'new'.\n")
  svntest.main.run_svn(None, 'add', new_path)
  svntest.main.run_svn(None, 'rm', mu_path)

  ### We're not testing copied or moved paths

  svntest.main.run_svn(None, 'commit', '-m', 'Committing changes', wc_dir)
  svntest.main.run_svn(None, 'up', wc_dir)

  expected_output = make_git_diff_header(new_path, "new", "revision 0",
                                         "revision 2", add=True) + [
    "@@ -0,0 +1 @@\n",
    "+This is the file 'new'.\n",
  ] + make_git_diff_header(mu_path, "A/mu", "revision 1", "working copy",
                           delete=True) + [
    "@@ -1 +0,0 @@\n",
    "-This is the file 'mu'.\n",
  ] +  make_git_diff_header(iota_path, "iota", "revision 1",
                            "working copy") + [
    "@@ -1 +1,2 @@\n",
    " This is the file 'iota'.\n",
    "+Changed 'iota'.\n",
  ]

  expected = svntest.verify.UnorderedOutput(expected_output)

  svntest.actions.run_and_verify_svn(None, expected, [], 'diff',
                                     '--git',
                                     '--old', repo_url + '@1', '--new',
                                     wc_dir)

def diff_git_format_url_url(sbox):
  "create a diff in git unidiff format for url-url"
  sbox.build()
  wc_dir = sbox.wc_dir
  repo_url = sbox.repo_url
  iota_path = os.path.join(wc_dir, 'iota')
  mu_path = os.path.join(wc_dir, 'A', 'mu')
  new_path = os.path.join(wc_dir, 'new')
  svntest.main.file_append(iota_path, "Changed 'iota'.\n")
  svntest.main.file_append(new_path, "This is the file 'new'.\n")
  svntest.main.run_svn(None, 'add', new_path)
  svntest.main.run_svn(None, 'rm', mu_path)

  ### We're not testing copied or moved paths. When we do, we will not be
  ### able to identify them as copies/moves until we have editor-v2.

  svntest.main.run_svn(None, 'commit', '-m', 'Committing changes', wc_dir)
  svntest.main.run_svn(None, 'up', wc_dir)

  expected_output = make_git_diff_header("A/mu", "A/mu", "revision 1",
                                         "revision 2",
                                         delete=True) + [
    "@@ -1 +0,0 @@\n",
    "-This is the file 'mu'.\n",
    ] + make_git_diff_header("new", "new", "revision 0", "revision 2",
                             add=True) + [
    "@@ -0,0 +1 @@\n",
    "+This is the file 'new'.\n",
  ] +  make_git_diff_header("iota", "iota", "revision 1",
                            "revision 2") + [
    "@@ -1 +1,2 @@\n",
    " This is the file 'iota'.\n",
    "+Changed 'iota'.\n",
  ]

  expected = svntest.verify.UnorderedOutput(expected_output)

  svntest.actions.run_and_verify_svn(None, expected, [], 'diff',
                                     '--git',
                                     '--old', repo_url + '@1', '--new',
                                     repo_url + '@2')

# Regression test for an off-by-one error when printing intermediate context
# lines.
def diff_prop_missing_context(sbox):
  "diff for property has missing context"
  sbox.build()
  wc_dir = sbox.wc_dir

  iota_path = os.path.join(wc_dir, 'iota')
  prop_val = "".join([
       "line 1\n",
       "line 2\n",
       "line 3\n",
       "line 4\n",
       "line 5\n",
       "line 6\n",
       "line 7\n",
     ])
  svntest.main.run_svn(None,
                       "propset", "prop", prop_val, iota_path)

  expected_output = svntest.wc.State(wc_dir, {
      'iota'    : Item(verb='Sending'),
      })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  prop_val = "".join([
               "line 3\n",
               "line 4\n",
               "line 5\n",
               "line 6\n",
             ])
  svntest.main.run_svn(None,
                       "propset", "prop", prop_val, iota_path)
  expected_output = make_diff_header(iota_path, 'revision 2',
                                     'working copy') + [
    "\n",
    "Property changes on: %s\n" % iota_path.replace('\\', '/'),
    "___________________________________________________________________\n",
    "Modified: prop\n",
    "## -1,7 +1,4 ##\n",
    "-line 1\n",
    "-line 2\n",
    " line 3\n",
    " line 4\n",
    " line 5\n",
    " line 6\n",
    "-line 7\n",
  ]

  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'diff', iota_path)

def diff_prop_multiple_hunks(sbox):
  "diff for property with multiple hunks"
  sbox.build()
  wc_dir = sbox.wc_dir

  iota_path = os.path.join(wc_dir, 'iota')
  prop_val = "".join([
       "line 1\n",
       "line 2\n",
       "line 3\n",
       "line 4\n",
       "line 5\n",
       "line 6\n",
       "line 7\n",
       "line 8\n",
       "line 9\n",
       "line 10\n",
       "line 11\n",
       "line 12\n",
       "line 13\n",
     ])
  svntest.main.run_svn(None,
                       "propset", "prop", prop_val, iota_path)

  expected_output = svntest.wc.State(wc_dir, {
      'iota'    : Item(verb='Sending'),
      })
  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.tweak('iota', wc_rev=2)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  prop_val = "".join([
               "line 1\n",
               "line 2\n",
               "line 3\n",
               "Add a line here\n",
               "line 4\n",
               "line 5\n",
               "line 6\n",
               "line 7\n",
               "line 8\n",
               "line 9\n",
               "line 10\n",
               "And add a line here\n",
               "line 11\n",
               "line 12\n",
               "line 13\n",
             ])
  svntest.main.run_svn(None,
                       "propset", "prop", prop_val, iota_path)
  expected_output = make_diff_header(iota_path, 'revision 2',
                                     'working copy') + [
    "\n",
    "Property changes on: %s\n" % iota_path.replace('\\', '/'),
    "___________________________________________________________________\n",
    "Modified: prop\n",
    "## -1,6 +1,7 ##\n",
    " line 1\n",
    " line 2\n",
    " line 3\n",
    "+Add a line here\n",
    " line 4\n",
    " line 5\n",
    " line 6\n",
    "## -8,6 +9,7 ##\n",
    " line 8\n",
    " line 9\n",
    " line 10\n",
    "+And add a line here\n",
    " line 11\n",
    " line 12\n",
    " line 13\n",
  ]

  svntest.actions.run_and_verify_svn(None, expected_output, [],
                                     'diff', iota_path)
def diff_git_empty_files(sbox):
  "create a diff in git format for empty files"
  sbox.build()
  wc_dir = sbox.wc_dir
  iota_path = os.path.join(wc_dir, 'iota')
  new_path = os.path.join(wc_dir, 'new')
  svntest.main.file_write(iota_path, "")

  # Now commit the local mod, creating rev 2.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'iota' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  svntest.main.file_write(new_path, "")
  svntest.main.run_svn(None, 'add', new_path)
  svntest.main.run_svn(None, 'rm', iota_path)

  expected_output = make_git_diff_header(new_path, "new", "revision 0",
                                         "working copy",
                                         add=True, text_changes=False) + [
  ] + make_git_diff_header(iota_path, "iota", "revision 2", "working copy",
                           delete=True, text_changes=False)

  svntest.actions.run_and_verify_svn(None, expected_output, [], 'diff',
                                     '--git', wc_dir)

def diff_git_with_props(sbox):
  "create a diff in git format showing prop changes"
  sbox.build()
  wc_dir = sbox.wc_dir
  iota_path = os.path.join(wc_dir, 'iota')
  new_path = os.path.join(wc_dir, 'new')
  svntest.main.file_write(iota_path, "")

  # Now commit the local mod, creating rev 2.
  expected_output = svntest.wc.State(wc_dir, {
    'iota' : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    'iota' : Item(status='  ', wc_rev=2),
    })

  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  svntest.main.file_write(new_path, "")
  svntest.main.run_svn(None, 'add', new_path)
  svntest.main.run_svn(None, 'propset', 'svn:eol-style', 'native', new_path)
  svntest.main.run_svn(None, 'propset', 'svn:keywords', 'Id', iota_path)

  expected_output = make_git_diff_header(new_path, "new", "revision 0",
                                         "working copy",
                                         add=True, text_changes=False) + [
      "\n",
      "Property changes on: new\n",
      "___________________________________________________________________\n",
      "Added: svn:eol-style\n",
      "## -0,0 +1 ##\n",
      "+native\n",
  ] + make_git_diff_header(iota_path, "iota", "revision 1", "working copy",
                           text_changes=False) + [
      "\n",
      "Property changes on: iota\n",
      "___________________________________________________________________\n",
      "Added: svn:keywords\n",
      "## -0,0 +1 ##\n",
      "+Id\n",
  ]

  svntest.actions.run_and_verify_svn(None, expected_output, [], 'diff',
                                     '--git', wc_dir)

def diff_git_with_props_on_dir(sbox):
  "diff in git format showing prop changes on dir"
  sbox.build()
  wc_dir = sbox.wc_dir

  # Now commit the local mod, creating rev 2.
  expected_output = svntest.wc.State(wc_dir, {
    '.' : Item(verb='Sending'),
    })

  expected_status = svntest.actions.get_virginal_state(wc_dir, 1)
  expected_status.add({
    '' : Item(status='  ', wc_rev=2),
    })

  svntest.main.run_svn(None, 'ps', 'a','b', wc_dir)
  svntest.actions.run_and_verify_commit(wc_dir, expected_output,
                                        expected_status, None, wc_dir)

  was_cwd = os.getcwd()
  os.chdir(wc_dir)
  expected_output = make_git_diff_header(".", "", "revision 1",
                                         "revision 2",
                                         add=False, text_changes=False) + [
      "\n",
      "Property changes on: \n",
      "___________________________________________________________________\n",
      "Added: a\n",
      "## -0,0 +1 ##\n",
      "+b\n",
  ]

  svntest.actions.run_and_verify_svn(None, expected_output, [], 'diff',
                                     '-c2', '--git')
  os.chdir(was_cwd)

@Issue(3826)
def diff_abs_localpath_from_wc_folder(sbox):
  "diff absolute localpath from wc folder"
  sbox.build(read_only = True)
  wc_dir = sbox.wc_dir

  A_path = os.path.join(wc_dir, 'A')
  B_abs_path = os.path.abspath(os.path.join(wc_dir, 'A', 'B'))
  os.chdir(os.path.abspath(A_path))
  svntest.actions.run_and_verify_svn(None, None, [], 'diff', B_abs_path)
              diff_renamed_dir,
              diff_url_against_local_mods,
              diff_preexisting_rev_against_local_add,
              diff_git_format_wc_wc,
              diff_git_format_url_wc,
              diff_git_format_url_url,
              diff_prop_missing_context,
              diff_prop_multiple_hunks,
              diff_git_empty_files,
              diff_git_with_props,
              diff_git_with_props_on_dir,
              diff_abs_localpath_from_wc_folder,