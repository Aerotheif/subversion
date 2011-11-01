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
 * @file svn_client_private.h
 * @brief Subversion-internal client APIs.
 */

#ifndef SVN_CLIENT_PRIVATE_H
#define SVN_CLIENT_PRIVATE_H

#include <apr_pools.h>

#include "svn_client.h"
#include "svn_types.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** Return @c SVN_ERR_ILLEGAL_TARGET if TARGETS contains a mixture of
 * URLs and paths; otherwise return SVN_NO_ERROR.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_client__assert_homogeneous_target_type(const apr_array_header_t *targets);


/* Create a svn_client_status_t structure *CST for LOCAL_ABSPATH, shallow
 * copying data from *STATUS wherever possible and retrieving the other values
 * where needed. Perform temporary allocations in SCRATCH_POOL and allocate the
 * result in RESULT_POOL
 */
svn_error_t *
svn_client__create_status(svn_client_status_t **cst,
                          svn_wc_context_t *wc_ctx,
                          const char *local_abspath,
                          const svn_wc_status3_t *status,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);


/** Resolve @a peg to a repository location and open an RA session to there.
 * Set @a *target_p to the location and @a *session_p to the new session,
 * both allocated in @a result_pool.
 *
 * If @a peg->path_or_url is a URL then a peg revision kind of 'unspecified'
 * means 'head', otherwise it means 'base'.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_client__peg_resolve(svn_client_target_t **target_p,
                        svn_ra_session_t **session_p,
                        const svn_client_peg_t *peg,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);


/* This property marks a branch root. Branches with the same value of this
 * property are mergeable. */
#define SVN_PROP_BRANCH_ROOT "svn:ignore" /* ### should be "svn:branch-root" */

/* Set *MARKER to the branch root marker that is common to SOURCE and
 * TARGET, or to NULL if neither has such a marker.
 * If only one has such a marker or they are different, throw an error. */
svn_error_t *
svn_client__check_branch_root_marker(const char **marker,
                                     svn_client_target_t *source,
                                     svn_client_target_t *target,
                                     svn_client_ctx_t *ctx,
                                     apr_pool_t *pool);

/* Set *MERGEINFO to describe the merges into TARGET from (paths in the
 * history of) SOURCE_BRANCH. */
svn_error_t *
svn_client__get_source_target_mergeinfo(svn_mergeinfo_catalog_t *mergeinfo_cat,
                                        svn_client_target_t *target,
                                        svn_client_target_t *source_branch,
                                        svn_client_ctx_t *ctx,
                                        apr_pool_t *result_pool,
                                        apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CLIENT_PRIVATE_H */
