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

require "my-assertions"

require "svn/error"

class SvnErrorTest < Test::Unit::TestCase
  def test_error_name
    Svn::Error.constants.each do |const_name|
      if /\A[A-Z0-9_]+\z/ =~ const_name and
          Svn::Error.const_get(const_name).is_a?(Class)
        class_name = Svn::Util.to_ruby_class_name(const_name)
        assert_equal(Svn::Error.const_get(class_name),
                     Svn::Error.const_get(const_name))
      end
    end
  end
end
