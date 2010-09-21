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
 */

#include "Common.h"
#include "Pool.h"
#include "Types.h"

#include "svn_time.h"

namespace SVN
{

CommitInfo::CommitInfo(const svn_commit_info_t *info)
  : m_author(info->author),
    m_post_commit_err(info->post_commit_err == NULL ? ""
                                                : info->post_commit_err),
    m_repos_root(info->repos_root == NULL ? "" : info->repos_root),
    m_revision(info->revision)
{
  Pool pool;

  SVN_CPP_ERR(svn_time_from_cstring(&m_date, info->date, pool.pool()));
}

}
