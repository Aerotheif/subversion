#!/usr/bin/env python
#
# gen-javahl-errors.py: Generate a Java class containing an enum for the
#                       C error codes
#
# ====================================================================
# Copyright (c) 2007-2009 CollabNet.  All rights reserved.
#
# * This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
# * This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://subversion.tigris.org/.
# ====================================================================
#

import sys, os

try:
  from svn import core
except ImportError, e:
  sys.stderr.write("ERROR: Unable to import Subversion's Python bindings: '%s'\n" \
                   "Hint: Set your PYTHONPATH environment variable, or adjust your " \
                   "PYTHONSTARTUP\nfile to point to your Subversion install " \
                   "location's svn-python directory.\n" % e)
  sys.stderr.flush()
  sys.exit(1)

def get_errors():
  errs = {}
  for key in vars(core):
    if key.find('SVN_ERR_') == 0:
      try:
        val = int(vars(core)[key])
        errs[val] = key
      except:
        pass
  return errs

def gen_javahl_class(error_codes, output_filename):
  jfile = open(output_filename, 'w')
  jfile.write(
"""/** ErrorCodes.java - This file is autogenerated by gen-javahl-errors.py
 */

package org.tigris.subversion.javahl;

/**
 * Provide mappings from error codes generated by the C runtime to meaningful
 * Java values.  For a better description of each error, please see
 * svn_error_codes.h in the C source.
 */
public class ErrorCodes
{
""")

  keys = sorted(error_codes.keys())

  for key in keys:
    # Format the code name to be more Java-esque
    code_name = error_codes[key][8:].replace('_', ' ').title().replace(' ', '')
    code_name = code_name[0].lower() + code_name[1:]

    jfile.write("    public static final int %s = %d;\n" % (code_name, key))

  jfile.write("}\n")
  jfile.close()

if __name__ == "__main__":
  if len(sys.argv) > 1:
    output_filename = sys.argv[1]
  else:
    output_filename = os.path.join('..', '..', 'subversion', 'bindings',
                                   'javahl', 'src', 'org', 'tigris',
                                   'subversion', 'javahl', 'ErrorCodes.java')

  gen_javahl_class(get_errors(), output_filename)
