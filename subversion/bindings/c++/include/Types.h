/**
 * @copyright
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 * @endcopyright
 *
 * @file Types.h
 * @brief Some helpful types
 */

#ifndef TYPES_H
#define TYPES_H

#include "Revision.h"

#include <map>
#include <string>

namespace SVN
{

// Typedefs
typedef std::map<std::string, std::string> PropTable;


// C-struct wrapper classes
class CommitInfo
{
  private:
    Revision m_revision;
    apr_time_t m_date;
    std::string m_author;
    std::string m_post_commit_err;
    std::string m_repos_root;

  public:
    CommitInfo(const svn_commit_info_t *info);
};

}


#endif // TYPES_H
