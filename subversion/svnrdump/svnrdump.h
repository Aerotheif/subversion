/*
 *  svnrdump.h: Internal header file for svnrdump.
 *
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
 */


#ifndef SVNRDUMP_H
#define SVNRDUMP_H

/*** Includes. ***/
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_hash.h"
#include "svn_delta.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/**
 * Get a dump editor @a editor along with a @a edit_baton allocated in
 * @a pool.  The editor will write output to @a stream.  Use @a
 * cancel_func and @a cancel_baton to check for user cancellation of
 * the operation (for timely-but-safe termination).
 */
svn_error_t *
svn_rdump__get_dump_editor(const svn_delta_editor_t **editor,
                           void **edit_baton,
                           svn_stream_t *stream,
                           svn_cancel_func_t cancel_func,
                           void *cancel_baton,
                           apr_pool_t *pool);


/* Normalize the line ending style of the values of properties in PROPS
 * that "need translation" (according to svn_prop_needs_translation(),
 * currently all svn:* props) so that they contain only LF (\n) line endings.
 */
svn_error_t *
svn_rdump__normalize_props(apr_hash_t *props,
                           apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVNRDUMP_H */
