/**
 * @copyright
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
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
 * @file Pool.cpp
 * @brief Implementation of the class Pool
 */

#include "Pool.h"
#include "JNIUtil.h"
#include "JNIMutex.h"
#include "JNICriticalSection.h"
#include "svn_pools.h"

/**
 * Constructor to create one apr pool as the subpool of the global pool
 * store this pool as the request pool.
 */
SVN::Pool::Pool()
{
  JNICriticalSection criticalSection(*JNIUtil::getGlobalPoolMutex());
  m_pool = svn_pool_create(JNIUtil::getPool());
  JNIUtil::setRequestPool(this);
}

/**
 * Destructor to destroy the apr pool and to clear the request pool
 * pointer.
 */
SVN::Pool::~Pool()
{
  JNICriticalSection criticalSection(*JNIUtil::getGlobalPoolMutex());
  JNIUtil::setRequestPool(NULL);
  if (m_pool)
    svn_pool_destroy(m_pool);
}
