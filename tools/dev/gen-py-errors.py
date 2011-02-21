#!/usr/bin/env python
#
# gen-py-errors.py: Generate a python module which maps error names to numbers.
#                   (The purpose being easier writing of the python tests.)
#
# ====================================================================
#    Licensed to the Apache Software Foundation (ASF) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The ASF licenses this file
#    to you under the Apache License, Version 2.0 (the
#    "License"); you may not use this file except in compliance
#    with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing,
#    software distributed under the License is distributed on an
#    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#    KIND, either express or implied.  See the License for the
#    specific language governing permissions and limitations
#    under the License.
# ====================================================================
#
#
#  Meant to be run from the root of a Subversion working copy.  If anybody
#  wants to do some path magic to improve that use, feel free.

import sys, os
sys.path.append(os.path.join('subversion', 'bindings', 'swig',
                             'python', 'tests'))


import setup_path

header = '''#!/usr/bin/env python
### This file automatically generated by tools/dev/gen-py-error.py,
### which see for more information
###
### It is versioned for convenience.

'''


def write_output(errs, filename):
  out = open(filename, 'w')
  out.write(header)

  for name, val in errs:
    out.write('%s = %d\n' % (name, val))

  out.close()


def main(output_filename):
  import core

  errs = [e for e in dir(core.svn.core) if e.startswith('SVN_ERR_')]
  codes = []
  for e in errs:
    codes.append((e[8:], getattr(core.svn.core, e)))
  write_output(codes, output_filename)


if __name__ == '__main__':
  main(os.path.join('subversion', 'tests', 'cmdline', 'svntest', 'err.py'))
