#!/usr/bin/env python2
# -*- Python -*-
"""Transform find-fix.py output into Excellable csv."""

__date__ = "Time-stamp: <03/08/08 18:35:22 jrepenning>"[13:30]
__author__ = "Jack Repenning <jrepenning@collab.net>"

import getopt
import inspect
import os
import os.path
import pydoc
import re
import shutil
import string
import sys
import time

# Long options and their usage strings; "=" means it takes an argument.
# To get a list suitable for getopt.getopt(), just do
#
#   [x[0] for x in long_opts]
#
# Make sure to sacrifice a lamb to Guido for each element of the list.
long_opts = [
    ["doc",             """Optional, print pydocs."""],
    ["help",            """Optional, print usage (this text)."""],
    ["verbose",         """Optional, print more progress messages."""],
    ]

help    = 0
verbose = 0
me = os.path.basename(sys.argv[0])

DATA_FILE = "http://subversion.tigris.org/iz-data/query-set-1.tsv"

def main():
    """Run find-fix.py with arguments du jour for drawing pretty
manager-speak pictures."""

    global verbose

    try:
        opts, args = getopt.getopt(sys.argv[1:], "", [x[0] for x in long_opts])
    except getopt.GetoptError, e:
        print "Error: ", e.msg
        shortusage()
        print me + " --help for options."
        sys.exit(1)

    for opt, arg in opts:
        if opt == "--help":
            usage()
            sys.exit(0)
        elif opt == "--verbose":
            verbose = 1
        elif opt == "--doc":
            pydoc.doc(pydoc.importfile(sys.argv[0]))
            sys.exit(0)

    # do something fruitful with your life
    if len(args) == 0:
        args = ["query-set-1.tsv", "core-history.csv"]
    if len(args) != 2:
        print "%s: Wrong number of args." % me
        shortusage()
        sys.exit(1)

    if os.system("curl " + DATA_FILE + "> " + args[0]):
        os.system("wget " + DATA_FILE)

    outfile = file(args[1], "w")
    outfile.write("Date,found,fixed,inval,dup,other,remain\n")

    totalsre = re.compile("totals:.*found= +([0-9]+) +"
                          "fixed= +([0-9]+) +"
                          "inval= +([0-9]+) +"
                          "dup= +([0-9]+) +"
                          "other= +([0-9]+) +"
                          "remain= +([0-9]+)")
    for year in ("2001", "2002", "2003", "2004"):
        for month in ("01", "02", "03", "04", "05", "06", "07", "08",
                      "09", "10", "11", "12"):
            for dayrange in (("01", "08"),
                             ("08", "15"),
                             ("15", "22"),
                             ("22", "28")):
                ffpy = os.popen("python ./find-fix.py --m=beta "
                                "%s %s-%s-%s %s-%s-%s"
                                % (args[0],
                                   year, month, dayrange[0],
                                   year, month, dayrange[1]))
                if verbose:
                    print "ffpy: ", ffpy

                line = ffpy.readline()
                if verbose:
                    print "initial line is: ", line
                matches = totalsre.search(line)
                if verbose:
                    print "initial match is: ", matches
                while line and not matches:
                    if verbose:
                        print "%s: read line '%s'" % (me, line)
                    line = ffpy.readline()
                    matches = totalsre.search(line)

                ffpy.close()

                if verbose:
                    print "line is ", line

                if matches.group(1) != "0" \
                   or matches.group(2) != "0" \
                   or matches.group(3) != "0" \
                   or matches.group(4) != "0" \
                   or matches.group(5) != "0":

                    outfile.write("%s-%s-%s,%s,%s,%s,%s,%s,%s\n"
                                  % (year, month, dayrange[1],
                                     matches.group(1),
                                     matches.group(2),
                                     matches.group(3),
                                     matches.group(4),
                                     matches.group(5),
                                     matches.group(6),
                                     ))
                elif matches.group(6) != "0":
                    # quit at first nothing-done week
                    # allows slop in loop controls
                    break
    outfile.close()


def shortusage():
  "Print one-line usage summary."
  print "%s - %s" % (me, pydoc.synopsis(sys.argv[0]))

def usage():
  "Print multi-line usage tome."
  shortusage()
  print '''%s [opts] queryfile outfile
Option keywords may be abbreviated to any unique prefix.
Option order is not important.
Most options require "=xxx" arguments:''' % me
  for x in long_opts:
      padding_limit = 18
      if x[0][-1:] == '=':
          print "   --" + x[0][:-1],
          padding_limit = 19
      else:
          print "   --" + x[0],
      print (' ' * (padding_limit - len(x[0]))), x[1]

if __name__ == "__main__":
  main()
