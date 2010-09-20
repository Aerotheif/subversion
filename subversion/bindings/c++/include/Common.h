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

#ifndef COMMON_H
#define COMMON_H

#include "svn_error.h"

#include <exception>
#include <string>

namespace SVN
{

class Exception : public std::exception
{
  private:
    void assembleErrorMessage(svn_error_t *err, int depth,
                              apr_status_t parent_apr_err,
                              std::string &buffer);

    std::string m_description;
    std::string m_source;
    int m_apr_err;

  public:
    /** A constructor to build an exception from a Subversion error.  The
        Exception object will ensure the error is cleared. */
    Exception(svn_error_t *err);

    Exception(const std::string &message);

    virtual ~Exception() throw ();

    virtual const char *what() const throw();
    const std::string &getSource();
    int getAPRErr();
};

}

/** A statement macro similar to SVN_ERR() which checks for a Subversion
 * error, and if one exists, throws a C++ exception in its place. */
#define SVN_CPP_ERR(expr)                               \
  do {                                                  \
    svn_error_t *svn_err__temp = (expr);                \
    if (svn_err__temp)                                  \
      throw Exception(svn_err__temp);                   \
  } while (0)

#endif // COMMON_H
