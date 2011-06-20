#!/bin/sh
#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#
set -e

repo=http://svn.apache.org/repos/asf/subversion
svn=svn
olds=7

# Parse our arguments
while getopts "cd:t:s:o:" flag; do
  case $flag in
    d) dir="`cd $OPTARG && pwd`" ;; # abspath
    c) clean="1" ;;
    t) target="$OPTARG" ;;
    s) svn="$OPTARG" ;;
    o) olds="$OPTARG" ;;
  esac
done

# Setup directories
if [ -n "$dir" ]; then cd $dir; else dir="."; fi
if [ -d "roll" ]; then rm -rf roll; fi
mkdir roll
if [ ! -n "$target" ]; then
  if [ ! -d "target" ]; then mkdir target; fi
  target="target"
fi

abscwd=`cd $dir; pwd`

echo "Will place results in: $target"

# get youngest
head=`$svn info $repo/trunk | grep '^Revision' | cut -d ' ' -f 2`

# Get the latest versions of the rolling scripts
for i in release.py dist.sh
do
  $svn export -r $head $repo/trunk/tools/dist/$i@$head $dir/$i
done

# Create the environment
cd roll
echo '----------------building environment------------------'
../release.py --base-dir ${abscwd}/roll build-env

# Roll the tarballs
echo '-------------------rolling tarball--------------------'
../release.py --base-dir ${abscwd}/roll roll --branch trunk trunk-nightly $head
cd ..

# Create the information page
./gen_nightly_ann.py $head > index.html

# Move the results to the target location
echo '-------------------moving results---------------------'
if [ -f "$target/index.html" ]; then rm "$target/index.html"; fi
mv index.html "$target"
if [ ! -d "$target/dist" ]; then mkdir "$target/dist"; fi
if [ -d "$target/dist/r$head" ]; then rm -r "$target/dist/r$head"; fi
mv roll/deploy "$target/dist/r$head"

# Some static links for the most recent artefacts.
ln -sf "r$head" "$target/dist/current"
ls "$target/dist/r$head" | while read fname; do
  ln -sf "r$head/$fname" "$target/dist/$fname"
done

# Clean up old results
ls -t1 "$target/dist/" | sed -e "1,${olds}d" | while read d; do
  rm -rf "$target/dist/$d"
done

# Optionally remove our working directory
if [ -n "$clean" ]; then
  echo '--------------------cleaning up-----------------------'
  rm -rf roll
fi

echo '------------------------done--------------------------'
