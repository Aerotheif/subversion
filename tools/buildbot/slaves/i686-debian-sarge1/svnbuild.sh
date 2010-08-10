#!/bin/bash
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

set -x

echo "========= autogen.sh"
./autogen.sh || exit $?

echo "========= configure"
./configure --disable-static --enable-shared \
            --enable-maintainer-mode \
            --with-neon=/usr/local/neon-0.25.5 \
            --with-apxs=/usr/sbin/apxs \
            --without-berkeley-db \
            --with-apr=/usr/local/apr \
            --with-apr-util=/usr/local/apr || exit $?

echo "========= make"
make || exit $?

# echo "========= make swig-py"
# make swig-py || exit $?

# echo "========= make swig-pl"
# make swig-pl || exit $?

#echo "========= make swig-rb"
#make swig-rb || exit $?

exit 0
