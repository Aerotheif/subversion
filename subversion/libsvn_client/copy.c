/*
 * copy.c:  copy/move wrappers around wc 'copy' functionality.
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

/* ==================================================================== */



/*** Includes. ***/

#include <string.h>
#include "svn_client.h"
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_opt.h"
#include "svn_time.h"
#include "svn_props.h"
#include "svn_mergeinfo.h"
#include "svn_hash.h"
#include "svn_sorts.h"
#include "svn_pools.h"

#include "client.h"
#include "mergeinfo.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"
#include "private/svn_ra_private.h"
#include "private/svn_mergeinfo_private.h"
#include "private/svn_client_private.h"


/*
 * OUR BASIC APPROACH TO COPIES
 * ============================
 *
 * for each source/destination pair
 *   if (not exist src_path)
 *     return ERR_BAD_SRC error
 *
 *   if (exist dst_path)
 *     return ERR_OBSTRUCTION error
 *   else
 *     copy src_path into parent_of_dst_path as basename (dst_path)
 *
 *   if (this is a move)
 *     delete src_path
 */



/*** Code. ***/

/* Extend the mergeinfo for the single WC path TARGET_WCPATH, adding
   MERGEINFO to any mergeinfo pre-existing in the WC. */
static svn_error_t *
extend_wc_mergeinfo(const char *target_abspath,
                    apr_hash_t *mergeinfo,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  apr_hash_t *wc_mergeinfo;

  /* Get a fresh copy of the pre-existing state of the WC's mergeinfo
     updating it. */
  SVN_ERR(svn_client__parse_mergeinfo(&wc_mergeinfo, ctx->wc_ctx,
                                      target_abspath, pool, pool));

  /* Combine the provided mergeinfo with any mergeinfo from the WC. */
  if (wc_mergeinfo && mergeinfo)
    SVN_ERR(svn_mergeinfo_merge2(wc_mergeinfo, mergeinfo, pool, pool));
  else if (! wc_mergeinfo)
    wc_mergeinfo = mergeinfo;

  return svn_error_trace(
    svn_client__record_wc_mergeinfo(target_abspath, wc_mergeinfo,
                                    FALSE, ctx, pool));
}

/* Find the longest common ancestor of paths in COPY_PAIRS.  If
   SRC_ANCESTOR is NULL, ignore source paths in this calculation.  If
   DST_ANCESTOR is NULL, ignore destination paths in this calculation.
   COMMON_ANCESTOR will be the common ancestor of both the
   SRC_ANCESTOR and DST_ANCESTOR, and will only be set if it is not
   NULL.
 */
static svn_error_t *
get_copy_pair_ancestors(const apr_array_header_t *copy_pairs,
                        const char **src_ancestor,
                        const char **dst_ancestor,
                        const char **common_ancestor,
                        apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_client__copy_pair_t *first;
  const char *first_dst;
  const char *first_src;
  const char *top_dst;
  svn_boolean_t src_is_url;
  svn_boolean_t dst_is_url;
  char *top_src;
  int i;

  first = APR_ARRAY_IDX(copy_pairs, 0, svn_client__copy_pair_t *);

  /* Because all the destinations are in the same directory, we can easily
     determine their common ancestor. */
  first_dst = first->dst_abspath_or_url;
  dst_is_url = svn_path_is_url(first_dst);

  if (copy_pairs->nelts == 1)
    top_dst = apr_pstrdup(subpool, first_dst);
  else
    top_dst = dst_is_url ? svn_uri_dirname(first_dst, subpool)
                         : svn_dirent_dirname(first_dst, subpool);

  /* Sources can came from anywhere, so we have to actually do some
     work for them.  */
  first_src = first->src_abspath_or_url;
  src_is_url = svn_path_is_url(first_src);
  top_src = apr_pstrdup(subpool, first_src);
  for (i = 1; i < copy_pairs->nelts; i++)
    {
      /* We don't need to clear the subpool here for several reasons:
         1)  If we do, we can't use it to allocate the initial versions of
             top_src and top_dst (above).
         2)  We don't return any errors in the following loop, so we
             are guanteed to destroy the subpool at the end of this function.
         3)  The number of iterations is likely to be few, and the loop will
             be through quickly, so memory leakage will not be significant,
             in time or space.
      */
      const svn_client__copy_pair_t *pair =
        APR_ARRAY_IDX(copy_pairs, i, svn_client__copy_pair_t *);

      top_src = src_is_url
        ? svn_uri_get_longest_ancestor(top_src, pair->src_abspath_or_url,
                                       subpool)
        : svn_dirent_get_longest_ancestor(top_src, pair->src_abspath_or_url,
                                          subpool);
    }

  if (src_ancestor)
    *src_ancestor = apr_pstrdup(pool, top_src);

  if (dst_ancestor)
    *dst_ancestor = apr_pstrdup(pool, top_dst);

  if (common_ancestor)
    *common_ancestor =
               src_is_url
                    ? svn_uri_get_longest_ancestor(top_src, top_dst, pool)
                    : svn_dirent_get_longest_ancestor(top_src, top_dst, pool);

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


/* The guts of do_wc_to_wc_copies */
static svn_error_t *
do_wc_to_wc_copies_with_write_lock(const apr_array_header_t *copy_pairs,
                                   const char *dst_parent,
                                   svn_client_ctx_t *ctx,
                                   apr_pool_t *scratch_pool)
{
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_error_t *err = SVN_NO_ERROR;

  for (i = 0; i < copy_pairs->nelts; i++)
    {
      const char *dst_abspath;
      svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *);
      svn_pool_clear(iterpool);

      /* Check for cancellation */
      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      /* Perform the copy */
      dst_abspath = svn_dirent_join(pair->dst_parent_abspath, pair->base_name,
                                    iterpool);
      err = svn_wc_copy3(ctx->wc_ctx, pair->src_abspath_or_url, dst_abspath,
                         FALSE /* metadata_only */,
                         ctx->cancel_func, ctx->cancel_baton,
                         ctx->notify_func2, ctx->notify_baton2, iterpool);
      if (err)
        break;
    }
  svn_pool_destroy(iterpool);

  svn_io_sleep_for_timestamps(dst_parent, scratch_pool);
  SVN_ERR(err);
  return SVN_NO_ERROR;
}

/* Copy each COPY_PAIR->SRC into COPY_PAIR->DST.  Use POOL for temporary
   allocations. */
static svn_error_t *
do_wc_to_wc_copies(const apr_array_header_t *copy_pairs,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  const char *dst_parent, *dst_parent_abspath;

  SVN_ERR(get_copy_pair_ancestors(copy_pairs, NULL, &dst_parent, NULL, pool));
  if (copy_pairs->nelts == 1)
    dst_parent = svn_dirent_dirname(dst_parent, pool);

  SVN_ERR(svn_dirent_get_absolute(&dst_parent_abspath, dst_parent, pool));

  SVN_WC__CALL_WITH_WRITE_LOCK(
    do_wc_to_wc_copies_with_write_lock(copy_pairs, dst_parent, ctx, pool),
    ctx->wc_ctx, dst_parent_abspath, FALSE, pool);

  return SVN_NO_ERROR;
}

/* The locked bit of do_wc_to_wc_moves. */
static svn_error_t *
do_wc_to_wc_moves_with_locks2(svn_client__copy_pair_t *pair,
                              const char *dst_parent_abspath,
                              svn_boolean_t lock_src,
                              svn_boolean_t lock_dst,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *scratch_pool)
{
  const char *dst_abspath;

  dst_abspath = svn_dirent_join(dst_parent_abspath, pair->base_name,
                                scratch_pool);

  SVN_ERR(svn_wc_move(ctx->wc_ctx, pair->src_abspath_or_url,
                     dst_abspath, FALSE /* metadata_only */,
                     ctx->cancel_func, ctx->cancel_baton,
                     ctx->notify_func2, ctx->notify_baton2,
                     scratch_pool));

  return SVN_NO_ERROR;
}

/* Wrapper to add an optional second lock */
static svn_error_t *
do_wc_to_wc_moves_with_locks1(svn_client__copy_pair_t *pair,
                              const char *dst_parent_abspath,
                              svn_boolean_t lock_src,
                              svn_boolean_t lock_dst,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *scratch_pool)
{
  if (lock_dst)
    SVN_WC__CALL_WITH_WRITE_LOCK(
      do_wc_to_wc_moves_with_locks2(pair, dst_parent_abspath, lock_src,
                                    lock_dst, ctx, scratch_pool),
      ctx->wc_ctx, dst_parent_abspath, FALSE, scratch_pool);
  else
    SVN_ERR(do_wc_to_wc_moves_with_locks2(pair, dst_parent_abspath, lock_src,
                                          lock_dst, ctx, scratch_pool));

  return SVN_NO_ERROR;
}

/* Move each COPY_PAIR->SRC into COPY_PAIR->DST, deleting COPY_PAIR->SRC
   afterwards.  Use POOL for temporary allocations. */
static svn_error_t *
do_wc_to_wc_moves(const apr_array_header_t *copy_pairs,
                  const char *dst_path,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  int i;
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_error_t *err = SVN_NO_ERROR;

  for (i = 0; i < copy_pairs->nelts; i++)
    {
      const char *src_parent_abspath;
      svn_boolean_t lock_src, lock_dst;

      svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *);
      svn_pool_clear(iterpool);

      /* Check for cancellation */
      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      src_parent_abspath = svn_dirent_dirname(pair->src_abspath_or_url,
                                              iterpool);

      /* We now need to lock the right combination of batons.
         Four cases:
           1) src_parent == dst_parent
           2) src_parent is parent of dst_parent
           3) dst_parent is parent of src_parent
           4) src_parent and dst_parent are disjoint
         We can handle 1) as either 2) or 3) */
      if (strcmp(src_parent_abspath, pair->dst_parent_abspath) == 0
          || svn_dirent_is_child(src_parent_abspath, pair->dst_parent_abspath,
                                 iterpool))
        {
          lock_src = TRUE;
          lock_dst = FALSE;
        }
      else if (svn_dirent_is_child(pair->dst_parent_abspath,
                                   src_parent_abspath,
                                   iterpool))
        {
          lock_src = FALSE;
          lock_dst = TRUE;
        }
      else
        {
          lock_src = TRUE;
          lock_dst = TRUE;
        }

      /* Perform the copy and then the delete. */
      if (lock_src)
        SVN_WC__CALL_WITH_WRITE_LOCK(
          do_wc_to_wc_moves_with_locks1(pair, pair->dst_parent_abspath,
                                        lock_src, lock_dst, ctx, iterpool),
          ctx->wc_ctx, src_parent_abspath,
          FALSE, iterpool);
      else
        SVN_ERR(do_wc_to_wc_moves_with_locks1(pair, pair->dst_parent_abspath,
                                              lock_src, lock_dst, ctx,
                                              iterpool));

    }
  svn_pool_destroy(iterpool);

  svn_io_sleep_for_timestamps(dst_path, pool);

  return svn_error_trace(err);
}


static svn_error_t *
verify_wc_srcs_and_dsts(const apr_array_header_t *copy_pairs,
                        svn_boolean_t make_parents,
                        svn_boolean_t is_move,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *pool)
{
  int i;
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* Check that all of our SRCs exist, and all the DSTs don't. */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *);
      svn_node_kind_t dst_kind, dst_parent_kind;

      svn_pool_clear(iterpool);

      /* Verify that SRC_PATH exists. */
      SVN_ERR(svn_io_check_path(pair->src_abspath_or_url, &pair->src_kind,
                                iterpool));
      if (pair->src_kind == svn_node_none)
        return svn_error_createf(
          SVN_ERR_NODE_UNKNOWN_KIND, NULL,
          _("Path '%s' does not exist"),
          svn_dirent_local_style(pair->src_abspath_or_url, pool));

      /* If DST_PATH does not exist, then its basename will become a new
         file or dir added to its parent (possibly an implicit '.').
         Else, just error out. */
      SVN_ERR(svn_io_check_path(pair->dst_abspath_or_url, &dst_kind,
                                iterpool));
      if (dst_kind != svn_node_none)
        {
          if (is_move
              && copy_pairs->nelts == 1
              && strcmp(svn_dirent_dirname(pair->src_abspath_or_url, iterpool),
                        svn_dirent_dirname(pair->dst_abspath_or_url,
                                           iterpool)) == 0)
            {
              const char *dst;
              char *dst_apr;
              apr_status_t apr_err;
              /* We have a rename inside a directory, which might collide
                 just because the case insensivity of the filesystem makes
                 the source match the destination. */

              SVN_ERR(svn_path_cstring_from_utf8(&dst,
                                                 pair->dst_abspath_or_url,
                                                 pool));

              apr_err = apr_filepath_merge(&dst_apr, NULL, dst,
                                           APR_FILEPATH_TRUENAME, iterpool);

              if (!apr_err)
                {
                  /* And now bring it back to our canonical format */
                  SVN_ERR(svn_path_cstring_to_utf8(&dst, dst_apr, iterpool));
                  dst = svn_dirent_canonicalize(dst, iterpool);
                }
              /* else: Don't report this error; just report the normal error */

              if (!apr_err && strcmp(dst, pair->src_abspath_or_url) == 0)
                {
                  /* Ok, we have a single case only rename. Get out of here */
                  svn_dirent_split(&pair->dst_parent_abspath, &pair->base_name,
                                   pair->dst_abspath_or_url, pool);

                  svn_pool_destroy(iterpool);
                  return SVN_NO_ERROR;
                }
            }

          return svn_error_createf(
                      SVN_ERR_ENTRY_EXISTS, NULL,
                      _("Path '%s' already exists"),
                      svn_dirent_local_style(pair->dst_abspath_or_url, pool));
        }

      svn_dirent_split(&pair->dst_parent_abspath, &pair->base_name,
                       pair->dst_abspath_or_url, pool);

      /* Make sure the destination parent is a directory and produce a clear
         error message if it is not. */
      SVN_ERR(svn_io_check_path(pair->dst_parent_abspath, &dst_parent_kind,
                                iterpool));
      if (make_parents && dst_parent_kind == svn_node_none)
        {
          SVN_ERR(svn_client__make_local_parents(pair->dst_parent_abspath,
                                                 TRUE, ctx, iterpool));
        }
      else if (make_parents && dst_parent_kind == svn_node_dir)
        {
          /* Check if parent is already versioned */
          SVN_ERR(svn_wc_read_kind(&dst_parent_kind, ctx->wc_ctx,
                                   pair->dst_parent_abspath, FALSE, iterpool));

          if (dst_parent_kind == svn_node_none)
            SVN_ERR(svn_client__make_local_parents(pair->dst_parent_abspath,
                                                   TRUE, ctx, iterpool));
        }
      else if (dst_parent_kind != svn_node_dir)
        {
          return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                                   _("Path '%s' is not a directory"),
                                   svn_dirent_local_style(
                                     pair->dst_parent_abspath, pool));
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Path-specific state used as part of path_driver_cb_baton. */
typedef struct path_driver_info_t
{
  const char *src_relpath;
  const char *dst_relpath;
  svn_node_kind_t src_kind;
  svn_revnum_t src_revnum;
  apr_hash_t *props;
} path_driver_info_t;


/* Starting with the path DIR relative to the RA_SESSION's session
   URL, work up through DIR's parents until an existing node is found.
   Push each nonexistent path onto the array NEW_DIRS, allocating in
   POOL.  Raise an error if the existing node is not a directory.

   ### Multiple requests for HEAD (SVN_INVALID_REVNUM) make this
   ### implementation susceptible to race conditions.  */
static svn_error_t *
find_absent_parents1(svn_ra_session_t *ra_session,
                     const char *dir,
                     apr_array_header_t *new_dirs,
                     apr_pool_t *pool)
{
  svn_node_kind_t kind;
  apr_pool_t *iterpool = svn_pool_create(pool);

  SVN_ERR(svn_ra_check_path(ra_session, dir, SVN_INVALID_REVNUM, &kind,
                            iterpool));

  while (kind == svn_node_none)
    {
      svn_pool_clear(iterpool);

      APR_ARRAY_PUSH(new_dirs, const char *) = dir;
      dir = svn_dirent_dirname(dir, pool);

      SVN_ERR(svn_ra_check_path(ra_session, dir, SVN_INVALID_REVNUM,
                                &kind, iterpool));
    }

  if (kind != svn_node_dir)
    return svn_error_createf(SVN_ERR_FS_ALREADY_EXISTS, NULL,
                             _("Path '%s' already exists, but is not a "
                               "directory"), dir);

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Starting with the URL *TOP_DST_URL which is also the root of
   RA_SESSION, work up through its parents until an existing node is
   found. Push each nonexistent URL onto the array NEW_DIRS,
   allocating in POOL.  Raise an error if the existing node is not a
   directory.

   Set *TOP_DST_URL and the RA session's root to the existing node's URL.

   ### Multiple requests for HEAD (SVN_INVALID_REVNUM) make this
   ### implementation susceptible to race conditions.  */
static svn_error_t *
find_absent_parents2(svn_ra_session_t *ra_session,
                     const char **top_dst_url,
                     apr_array_header_t *new_dirs,
                     apr_pool_t *pool)
{
  const char *root_url = *top_dst_url;
  svn_node_kind_t kind;

  SVN_ERR(svn_ra_check_path(ra_session, "", SVN_INVALID_REVNUM, &kind,
                            pool));

  while (kind == svn_node_none)
    {
      APR_ARRAY_PUSH(new_dirs, const char *) = root_url;
      root_url = svn_uri_dirname(root_url, pool);

      SVN_ERR(svn_ra_reparent(ra_session, root_url, pool));
      SVN_ERR(svn_ra_check_path(ra_session, "", SVN_INVALID_REVNUM, &kind,
                                pool));
    }

  if (kind != svn_node_dir)
    return svn_error_createf(SVN_ERR_FS_ALREADY_EXISTS, NULL,
                _("Path '%s' already exists, but is not a directory"),
                root_url);

  *top_dst_url = root_url;
  return SVN_NO_ERROR;
}


static svn_error_t *
drive_single_path(svn_editor_t *editor,
                  path_driver_info_t *path_info,
                  svn_boolean_t is_move,
                  const char *repos_root,
                  apr_pool_t *scratch_pool)
{
  if (is_move)
    {
      SVN_ERR(svn_editor_move(editor, path_info->src_relpath,
                              path_info->src_revnum, path_info->dst_relpath,
                              SVN_INVALID_REVNUM));
    }
  else
    {
      SVN_ERR(svn_editor_copy(editor, path_info->src_relpath,
                              path_info->src_revnum, path_info->dst_relpath,
                              SVN_INVALID_REVNUM));
    }

  if (path_info->props)
    {
      if (path_info->src_kind == svn_node_file)
        SVN_ERR(svn_editor_alter_file(editor, path_info->dst_relpath,
                                      SVN_INVALID_REVNUM, path_info->props,
                                      NULL, NULL));
      else
        SVN_ERR(svn_editor_alter_directory(editor, path_info->dst_relpath,
                                           SVN_INVALID_REVNUM,
                                           path_info->props));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
drive_editor(svn_editor_t *editor,
             apr_array_header_t *new_dirs,
             apr_array_header_t *path_infos,
             svn_boolean_t is_move,
             const char *repos_root,
             apr_pool_t *scratch_pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  apr_pool_t *iterpool;
  int i;

  if (new_dirs)
    {
      apr_hash_t *props = apr_hash_make(scratch_pool);

      /* These are directories which we just need to add. */
      for (i = 0; i < new_dirs->nelts; i++)
        {
          const char *dir = APR_ARRAY_IDX(new_dirs, i, const char *);
          apr_array_header_t *children = apr_array_make(scratch_pool, 1,
                                                        sizeof(const char *));

          if (i < new_dirs->nelts - 1)
            {
              /* The only child of this directory is the next one in the
                 list. */
              APR_ARRAY_PUSH(children, const char *) =
                    svn_relpath_basename(APR_ARRAY_IDX(new_dirs, i + 1,
                                                       const char *),
                                         scratch_pool);
            }
          else
            {
              /* Handle the last directory.  It's children list needs to
                 include all the of the various dest paths. */
              int j;

              for (j = 0; j < path_infos->nelts; j++)
                {
                  path_driver_info_t *info = APR_ARRAY_IDX(path_infos, j,
                                                        path_driver_info_t *);

                  APR_ARRAY_PUSH(children, const char *) =
                    svn_relpath_basename(info->dst_relpath, scratch_pool);
                }
            }
          SVN_ERR(svn_editor_add_directory(editor, dir, children, props,
                                           SVN_INVALID_REVNUM));
        }

    }

  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < path_infos->nelts; i++)
    {
      path_driver_info_t *path_info = APR_ARRAY_IDX(path_infos, i,
                                                    path_driver_info_t *);

      svn_pool_clear(iterpool);

      err = drive_single_path(editor, path_info, is_move, repos_root,
                              iterpool);
      if (err)
        break;
    }
  svn_pool_destroy(iterpool);

  if (err)
    {
      /* At least try to abort the edit (and fs txn) before throwing err. */
      svn_error_clear(svn_editor_abort(editor));
      return svn_error_trace(err);
    }

  /* Complete the edit. */
  return svn_error_trace(svn_editor_complete(editor));
}

static svn_error_t *
fetch_props_func(apr_hash_t **props,
                 void *baton,
                 const char *path,
                 svn_revnum_t base_revision,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  *props = apr_hash_make(result_pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
fetch_kind_func(svn_kind_t *kind,
                void *baton,
                const char *path,
                svn_revnum_t base_revision,
                apr_pool_t *scratch_pool)
{
  apr_array_header_t *path_infos = baton;
  int i;

  /* Find the correct path_info, and return its source kind.  While iterating
     over the list is O(n), in practice this is a very short list (and this
     is temporary code anyway). */
  for (i = 0; i < path_infos->nelts; i++)
    {
      path_driver_info_t *path_info = APR_ARRAY_IDX(path_infos, i,
                                                    path_driver_info_t *);

      if (strcmp(path_info->src_relpath, path) == 0)
        {
          *kind = svn__kind_from_node_kind(path_info->src_kind, FALSE);
          return SVN_NO_ERROR;
        }
    }

  *kind = svn_kind_unknown;
  return SVN_NO_ERROR;
}

static svn_error_t *
fetch_base_func(const char **filename,
                void *baton,
                const char *path,
                svn_revnum_t base_revision,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  *filename = NULL;
  return SVN_NO_ERROR;
}

static svn_error_t *
repos_to_repos_copy(const apr_array_header_t *copy_pairs,
                    svn_boolean_t make_parents,
                    const apr_hash_t *revprop_table,
                    svn_commit_callback2_t commit_callback,
                    void *commit_baton,
                    svn_client_ctx_t *ctx,
                    svn_boolean_t is_move,
                    apr_pool_t *pool)
{
  apr_array_header_t *path_infos;
  const char *message, *repos_root;
  svn_ra_session_t *ra_session = NULL;
  svn_editor_t *editor;
  apr_array_header_t *new_dirs = NULL;
  apr_hash_t *commit_revprops;
  int i;
  svn_delta_shim_callbacks_t *shim_callbacks;
  svn_client__copy_pair_t *first_pair =
    APR_ARRAY_IDX(copy_pairs, 0, svn_client__copy_pair_t *);

  /* Open an RA session to the first copy pair's destination.  We'll
     be verifying that every one of our copy source and destination
     URLs is or is beneath this sucker's repository root URL as a form
     of a cheap(ish) sanity check.  */
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, NULL,
                                               first_pair->src_abspath_or_url,
                                               NULL, NULL, FALSE, TRUE,
                                               ctx, pool));
  SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root, pool));

  /* We're going to reparent the session to the repos_root, and then drive
     the Ev2 from there.  This might have issue #3242 implications. */
  SVN_ERR(svn_ra_reparent(ra_session, repos_root, pool));

  /* Verify that sources and destinations are all at or under
     REPOS_ROOT.  While here, create a path_info struct for each
     src/dst pair and initialize portions of it with normalized source
     location information.  */
  path_infos = apr_array_make(pool, copy_pairs->nelts,
                              sizeof(path_driver_info_t *));
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      path_driver_info_t *info = apr_pcalloc(pool, sizeof(*info));
      svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *);
      apr_hash_t *mergeinfo;
      svn_node_kind_t dst_kind;

      /* Are the source and destination URLs at or under REPOS_ROOT? */
      if (! (svn_uri__is_ancestor(repos_root, pair->src_abspath_or_url)
             && svn_uri__is_ancestor(repos_root, pair->dst_abspath_or_url)))
        return svn_error_create
          (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
           _("Source and destination URLs appear not to point to the "
             "same repository."));

      /* Run the history function to get the source's URL and revnum in the
         operational revision. */
      /* ### Gah!  We have to wrap this call in a reparent... */
      SVN_ERR(svn_ra_reparent(ra_session, pair->src_abspath_or_url, pool));
      SVN_ERR(svn_client__repos_locations(&pair->src_abspath_or_url,
                                          &pair->src_revnum,
                                          NULL, NULL,
                                          ra_session,
                                          pair->src_abspath_or_url,
                                          &pair->src_peg_revision,
                                          &pair->src_op_revision, NULL,
                                          ctx, pool));
      info->src_revnum = pair->src_revnum;
      SVN_ERR(svn_ra_reparent(ra_session, repos_root, pool));

      /* Calculate and check relpaths. */
      info->src_relpath = svn_uri_skip_ancestor(repos_root,
                                                pair->src_abspath_or_url,
                                                pool);
      info->dst_relpath = svn_uri_skip_ancestor(repos_root,
                                                pair->dst_abspath_or_url,
                                                pool);
      SVN_ERR(svn_path_check_valid(info->dst_relpath, pool));
      SVN_ERR(svn_path_check_valid(info->src_relpath, pool));

      /* Verify that the source exists and the proposed destination does not,
         and toss what we've learned into the INFO array.  (For copies --
         that is, non-moves -- the relative source URL NULL because it isn't
         a child of the TOP_URL at all.  That's okay, we'll deal with it.)  */
      SVN_ERR(svn_ra_check_path(ra_session, info->src_relpath,
                                pair->src_revnum, &info->src_kind, pool));

      if (info->src_kind == svn_node_none)
        return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                                 _("Path '%s' does not exist in revision %ld"),
                                 pair->src_abspath_or_url, pair->src_revnum);

      /* Ensure that we aren't trying to overwrite existing paths.  */
      SVN_ERR(svn_ra_check_path(ra_session, info->dst_relpath,
                                SVN_INVALID_REVNUM, &dst_kind, pool));
      if (dst_kind != svn_node_none)
        return svn_error_createf(SVN_ERR_FS_ALREADY_EXISTS, NULL,
                                 _("Path '%s' already exists"),
                                 info->dst_relpath);

      /* Go ahead and grab mergeinfo from the source, too. */
      SVN_ERR(svn_client__get_repos_mergeinfo(
                &mergeinfo, ra_session,
                pair->src_abspath_or_url, pair->src_revnum,
                svn_mergeinfo_inherited, TRUE /*squelch_incapable*/, pool));

      if (mergeinfo)
        {
          apr_hash_t *props;
          apr_array_header_t *proplist;
          svn_string_t *mergeinfo_str;

          /* We need to fetch and filter props, so we can set props together,
             as dictated by Ev2. */
          SVN_ERR(svn_mergeinfo_to_string(&mergeinfo_str, mergeinfo, pool));

          if (info->src_kind == svn_node_file)
            SVN_ERR(svn_ra_get_file(ra_session, info->src_relpath,
                                    info->src_revnum, NULL, NULL, &props,
                                    pool));
          else
            SVN_ERR(svn_ra_get_dir2(ra_session, NULL, NULL, &props,
                                    info->src_relpath, info->src_revnum, 0,
                                    pool));

          proplist = svn_prop_hash_to_array(props, pool);
          SVN_ERR(svn_categorize_props(proplist, NULL, NULL, &proplist, pool));
          info->props = svn_prop_array_to_hash(proplist, pool);

          apr_hash_set(info->props, SVN_PROP_MERGEINFO, APR_HASH_KEY_STRING,
                       mergeinfo_str);
        }

      /* Finally, plop an INFO structure onto our array thereof. */
      APR_ARRAY_PUSH(path_infos, path_driver_info_t *) = info;
    }

  /* If we're allowed to create nonexistent parent directories of our
     destinations, then make a list in NEW_DIRS of the parent
     directories of the destination that don't yet exist.  */
  if (make_parents)
    {
      const char *dir;
      path_driver_info_t *info = APR_ARRAY_IDX(path_infos, 0,
                                               path_driver_info_t *);

      new_dirs = apr_array_make(pool, 0, sizeof(const char *));

      /* We take advantage of the knowledge of our caller.  We know that
         if there are multiple copy/move operations being requested, then the
         destinations of the copies/moves will all be siblings of one
         another.  Therefore, we need only to check for the
         nonexistent paths between TOP_URL and *one* of our
         destinations to find nonexistent parents of all of them.  */
      dir = svn_relpath_dirname(info->dst_relpath, pool);
      SVN_ERR(find_absent_parents1(ra_session, dir, new_dirs, pool));

      qsort(new_dirs->elts, new_dirs->nelts, new_dirs->elt_size,
            svn_sort_compare_paths);
    }

  if (SVN_CLIENT__HAS_LOG_MSG_FUNC(ctx))
    {
      /* Produce a list of new paths to add, and provide it to the
         mechanism used to acquire a log message. */
      svn_client_commit_item3_t *item;
      const char *tmp_file;
      apr_array_header_t *commit_items
        = apr_array_make(pool, 2 * copy_pairs->nelts, sizeof(item));

      /* Add any intermediate directories to the message */
      if (make_parents)
        {
          for (i = 0; i < new_dirs->nelts; i++)
            {
              const char *relpath = APR_ARRAY_IDX(new_dirs, i, const char *);

              item = svn_client_commit_item3_create(pool);
              item->url = svn_path_url_add_component2(repos_root, relpath,
                                                      pool);
              item->state_flags = SVN_CLIENT_COMMIT_ITEM_ADD;
              APR_ARRAY_PUSH(commit_items, svn_client_commit_item3_t *) = item;
            }
        }

      for (i = 0; i < path_infos->nelts; i++)
        {
          path_driver_info_t *info = APR_ARRAY_IDX(path_infos, i,
                                                   path_driver_info_t *);

          item = svn_client_commit_item3_create(pool);
          item->url = svn_path_url_add_component2(repos_root,
                                                  info->dst_relpath, pool);
          item->state_flags = SVN_CLIENT_COMMIT_ITEM_ADD;
          APR_ARRAY_PUSH(commit_items, svn_client_commit_item3_t *) = item;

          if (is_move)
            {
              item = apr_pcalloc(pool, sizeof(*item));
              item->url = svn_path_url_add_component2(repos_root,
                                                      info->src_relpath, pool);
              item->state_flags = SVN_CLIENT_COMMIT_ITEM_DELETE;
              APR_ARRAY_PUSH(commit_items, svn_client_commit_item3_t *) = item;
            }
        }

      SVN_ERR(svn_client__get_log_msg(&message, &tmp_file, commit_items,
                                      ctx, pool));
      if (! message)
        return SVN_NO_ERROR;
    }
  else
    message = "";

  SVN_ERR(svn_client__ensure_revprop_table(&commit_revprops, revprop_table,
                                           message, ctx, pool));

  /* Fetch RA commit editor. */
  shim_callbacks = svn_delta_shim_callbacks_default(pool);
  SVN_ERR(svn_ra__register_editor_shim_callbacks(ra_session, shim_callbacks));
  shim_callbacks->fetch_props_func = fetch_props_func;
  shim_callbacks->fetch_base_func = fetch_base_func;
  shim_callbacks->fetch_kind_func = fetch_kind_func;
  shim_callbacks->fetch_baton = path_infos;

  SVN_ERR(svn_ra_get_commit_editor4(ra_session, &editor,
                                    commit_revprops,
                                    commit_callback,
                                    commit_baton,
                                    NULL, TRUE, /* No lock tokens */
                                    ctx->cancel_func, ctx->cancel_baton,
                                    pool, pool));

  return svn_error_trace(drive_editor(editor, new_dirs, path_infos, is_move,
                                      repos_root, pool));
}

/* Baton for check_url_kind */
struct check_url_kind_baton
{
  svn_ra_session_t *session;
  const char *repos_root_url;
  svn_boolean_t should_reparent;
};

/* Implements svn_client__check_url_kind_t for wc_to_repos_copy */
static svn_error_t *
check_url_kind(void *baton,
               svn_node_kind_t *kind,
               const char *url,
               svn_revnum_t revision,
               apr_pool_t *scratch_pool)
{
  struct check_url_kind_baton *cukb = baton;

  /* If we don't have a session or can't use the session, get one */
  if (!svn_uri__is_ancestor(cukb->repos_root_url, url))
    *kind = svn_node_none;
  else
    {
      cukb->should_reparent = TRUE;

      SVN_ERR(svn_ra_reparent(cukb->session, url, scratch_pool));

      SVN_ERR(svn_ra_check_path(cukb->session, "", revision,
                                kind, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* ### Copy ...
 * COMMIT_INFO_P is ...
 * COPY_PAIRS is ... such that each 'src_abspath_or_url' is a local abspath
 * and each 'dst_abspath_or_url' is a URL.
 * MAKE_PARENTS is ...
 * REVPROP_TABLE is ...
 * CTX is ... */
static svn_error_t *
wc_to_repos_copy(const apr_array_header_t *copy_pairs,
                 svn_boolean_t make_parents,
                 const apr_hash_t *revprop_table,
                 svn_commit_callback2_t commit_callback,
                 void *commit_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  const char *message;
  const char *top_src_path, *top_dst_url;
  struct check_url_kind_baton cukb;
  const char *top_src_abspath;
  svn_ra_session_t *ra_session;
  const svn_delta_editor_t *editor;
  apr_hash_t *relpath_map = NULL;
  void *edit_baton;
  svn_client__committables_t *committables;
  apr_array_header_t *commit_items;
  apr_pool_t *iterpool;
  apr_array_header_t *new_dirs = NULL;
  apr_hash_t *commit_revprops;
  svn_client__copy_pair_t *first_pair;
  int i;

  /* Find the common root of all the source paths */
  SVN_ERR(get_copy_pair_ancestors(copy_pairs, &top_src_path, NULL, NULL, pool));

  /* Do we need to lock the working copy?  1.6 didn't take a write
     lock, but what happens if the working copy changes during the copy
     operation? */

  iterpool = svn_pool_create(pool);

  /* Verify that all the source paths exist, are versioned, etc.
     We'll do so by querying the base revisions of those things (which
     we'll need to know later anyway).
     ### Should we use the 'origin' revision instead of 'base'?
    */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *);
      svn_pool_clear(iterpool);

      SVN_ERR(svn_wc__node_get_base(&pair->src_revnum, NULL, NULL, NULL,
                                    ctx->wc_ctx, pair->src_abspath_or_url,
                                    iterpool, iterpool));
    }

  /* Determine the longest common ancestor for the destinations, and open an RA
     session to that location. */
  /* ### But why start by getting the _parent_ of the first one? */
  /* --- That works because multiple destinations always point to the same
   *     directory. I'm rather wondering why we need to find a common
   *     destination parent here at all, instead of simply getting
   *     top_dst_url from get_copy_pair_ancestors() above?
   *     It looks like the entire block of code hanging off this comment
   *     is redundant. */
  first_pair = APR_ARRAY_IDX(copy_pairs, 0, svn_client__copy_pair_t *);
  top_dst_url = svn_uri_dirname(first_pair->dst_abspath_or_url, pool);
  for (i = 1; i < copy_pairs->nelts; i++)
    {
      svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *);
      top_dst_url = svn_uri_get_longest_ancestor(top_dst_url,
                                                 pair->dst_abspath_or_url,
                                                 pool);
    }

  SVN_ERR(svn_dirent_get_absolute(&top_src_abspath, top_src_path, pool));
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, NULL, top_dst_url,
                                               top_src_abspath, NULL, TRUE,
                                               TRUE, ctx, pool));

  /* If requested, determine the nearest existing parent of the destination,
     and reparent the ra session there. */
  if (make_parents)
    {
      new_dirs = apr_array_make(pool, 0, sizeof(const char *));
      SVN_ERR(find_absent_parents2(ra_session, &top_dst_url, new_dirs, pool));
    }

  /* Figure out the basename that will result from each copy and check to make
     sure it doesn't exist already. */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      svn_node_kind_t dst_kind;
      const char *dst_rel;
      svn_client__copy_pair_t *pair =
        APR_ARRAY_IDX(copy_pairs, i, svn_client__copy_pair_t *);

      svn_pool_clear(iterpool);
      dst_rel = svn_uri_skip_ancestor(top_dst_url, pair->dst_abspath_or_url,
                                      iterpool);
      SVN_ERR(svn_ra_check_path(ra_session, dst_rel, SVN_INVALID_REVNUM,
                                &dst_kind, iterpool));
      if (dst_kind != svn_node_none)
        {
          return svn_error_createf(SVN_ERR_FS_ALREADY_EXISTS, NULL,
                                   _("Path '%s' already exists"),
                                   pair->dst_abspath_or_url);
        }
    }

  if (SVN_CLIENT__HAS_LOG_MSG_FUNC(ctx))
    {
      /* Produce a list of new paths to add, and provide it to the
         mechanism used to acquire a log message. */
      svn_client_commit_item3_t *item;
      const char *tmp_file;
      commit_items = apr_array_make(pool, copy_pairs->nelts, sizeof(item));

      /* Add any intermediate directories to the message */
      if (make_parents)
        {
          for (i = 0; i < new_dirs->nelts; i++)
            {
              const char *url = APR_ARRAY_IDX(new_dirs, i, const char *);

              item = svn_client_commit_item3_create(pool);
              item->url = url;
              item->state_flags = SVN_CLIENT_COMMIT_ITEM_ADD;
              APR_ARRAY_PUSH(commit_items, svn_client_commit_item3_t *) = item;
            }
        }

      for (i = 0; i < copy_pairs->nelts; i++)
        {
          svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                            svn_client__copy_pair_t *);

          item = svn_client_commit_item3_create(pool);
          item->url = pair->dst_abspath_or_url;
          item->state_flags = SVN_CLIENT_COMMIT_ITEM_ADD;
          APR_ARRAY_PUSH(commit_items, svn_client_commit_item3_t *) = item;
        }

      SVN_ERR(svn_client__get_log_msg(&message, &tmp_file, commit_items,
                                      ctx, pool));
      if (! message)
        {
          svn_pool_destroy(iterpool);
          return SVN_NO_ERROR;
        }
    }
  else
    message = "";

  SVN_ERR(svn_client__ensure_revprop_table(&commit_revprops, revprop_table,
                                           message, ctx, pool));

  cukb.session = ra_session;
  SVN_ERR(svn_ra_get_repos_root2(ra_session, &cukb.repos_root_url, pool));
  cukb.should_reparent = FALSE;

  /* Crawl the working copy for commit items. */
  /* ### TODO: Pass check_url_func for issue #3314 handling */
  SVN_ERR(svn_client__get_copy_committables(&committables,
                                            copy_pairs,
                                            check_url_kind, &cukb,
                                            ctx, pool, pool));

  /* The committables are keyed by the repository root */
  commit_items = apr_hash_get(committables->by_repository,
                              cukb.repos_root_url,
                              APR_HASH_KEY_STRING);
  SVN_ERR_ASSERT(commit_items != NULL);

  if (cukb.should_reparent)
    SVN_ERR(svn_ra_reparent(ra_session, top_dst_url, pool));

  /* If we are creating intermediate directories, tack them onto the list
     of committables. */
  if (make_parents)
    {
      for (i = 0; i < new_dirs->nelts; i++)
        {
          const char *url = APR_ARRAY_IDX(new_dirs, i, const char *);
          svn_client_commit_item3_t *item;

          item = svn_client_commit_item3_create(pool);
          item->url = url;
          item->state_flags = SVN_CLIENT_COMMIT_ITEM_ADD;
          item->incoming_prop_changes = apr_array_make(pool, 1,
                                                       sizeof(svn_prop_t *));
          APR_ARRAY_PUSH(commit_items, svn_client_commit_item3_t *) = item;
        }
    }

  /* ### TODO: This extra loop would be unnecessary if this code lived
     ### in svn_client__get_copy_committables(), which is incidentally
     ### only used above (so should really be in this source file). */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      apr_hash_t *mergeinfo, *wc_mergeinfo;
      svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *);
      svn_client_commit_item3_t *item =
        APR_ARRAY_IDX(commit_items, i, svn_client_commit_item3_t *);
      svn_client__pathrev_t *src_origin;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_client__wc_node_get_origin(&src_origin,
                                             pair->src_abspath_or_url,
                                             ctx, iterpool, iterpool));

      /* Set the mergeinfo for the destination to the combined merge
         info known to the WC and the repository. */
      item->outgoing_prop_changes = apr_array_make(pool, 1,
                                                   sizeof(svn_prop_t *));
      /* Repository mergeinfo (or NULL if it's locally added)... */
      if (src_origin)
        SVN_ERR(svn_client__get_repos_mergeinfo(
                  &mergeinfo, ra_session, src_origin->url, src_origin->rev,
                  svn_mergeinfo_inherited, TRUE /*sqelch_inc.*/, iterpool));
      else
        mergeinfo = NULL;
      /* ... and WC mergeinfo. */
      SVN_ERR(svn_client__parse_mergeinfo(&wc_mergeinfo, ctx->wc_ctx,
                                          pair->src_abspath_or_url,
                                          iterpool, iterpool));
      if (wc_mergeinfo && mergeinfo)
        SVN_ERR(svn_mergeinfo_merge2(mergeinfo, wc_mergeinfo, iterpool,
                                     iterpool));
      else if (! mergeinfo)
        mergeinfo = wc_mergeinfo;
      if (mergeinfo)
        {
          /* Push a mergeinfo prop representing MERGEINFO onto the
           * OUTGOING_PROP_CHANGES array. */

          svn_prop_t *mergeinfo_prop
            = apr_palloc(item->outgoing_prop_changes->pool,
                         sizeof(svn_prop_t));
          svn_string_t *prop_value;

          SVN_ERR(svn_mergeinfo_to_string(&prop_value, mergeinfo,
                                          item->outgoing_prop_changes->pool));

          mergeinfo_prop->name = SVN_PROP_MERGEINFO;
          mergeinfo_prop->value = prop_value;
          APR_ARRAY_PUSH(item->outgoing_prop_changes, svn_prop_t *)
            = mergeinfo_prop;
        }
    }

  /* Sort and condense our COMMIT_ITEMS. */
  SVN_ERR(svn_client__condense_commit_items(&top_dst_url,
                                            commit_items, pool));

#ifdef ENABLE_EV2_SHIMS
  if (commit_items)
    {
      relpath_map = apr_hash_make(pool);
      for (i = 0; i < commit_items->nelts; i++)
        {
          svn_client_commit_item3_t *item = APR_ARRAY_IDX(commit_items, i,
                                                  svn_client_commit_item3_t *);
          const char *relpath;

          if (!item->path)
            continue;

          svn_pool_clear(iterpool);
          SVN_ERR(svn_wc__node_get_origin(NULL, NULL, &relpath, NULL, NULL, NULL,
                                          ctx->wc_ctx, item->path, FALSE, pool,
                                          iterpool));
          if (relpath)
            apr_hash_set(relpath_map, relpath, APR_HASH_KEY_STRING, item->path);
        }
    }
#endif

  /* Open an RA session to DST_URL. */
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, NULL, top_dst_url,
                                               NULL, commit_items,
                                               FALSE, FALSE, ctx, pool));

  /* Fetch RA commit editor. */
  SVN_ERR(svn_ra__register_editor_shim_callbacks(ra_session,
                        svn_client__get_shim_callbacks(ctx->wc_ctx, relpath_map,
                                                       pool)));
  SVN_ERR(svn_ra_get_commit_editor3(ra_session, &editor, &edit_baton,
                                    commit_revprops,
                                    commit_callback,
                                    commit_baton, NULL,
                                    TRUE, /* No lock tokens */
                                    pool));

  /* Perform the commit. */
  SVN_ERR_W(svn_client__do_commit(top_dst_url, commit_items,
                                  editor, edit_baton,
                                  0, /* ### any notify_path_offset needed? */
                                  NULL, ctx, pool, pool),
            _("Commit failed (details follow):"));

  /* Sleep to ensure timestamp integrity. */
  svn_io_sleep_for_timestamps(top_src_path, pool);

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* A baton for notification_adjust_func(). */
struct notification_adjust_baton
{
  svn_wc_notify_func2_t inner_func;
  void *inner_baton;
  const char *checkout_abspath;
  const char *final_abspath;
};

/* A svn_wc_notify_func2_t function that wraps BATON->inner_func (whose
 * baton is BATON->inner_baton) and adjusts the notification paths that
 * start with BATON->checkout_abspath to start instead with
 * BATON->final_abspath. */
static void
notification_adjust_func(void *baton,
                         const svn_wc_notify_t *notify,
                         apr_pool_t *pool)
{
  struct notification_adjust_baton *nb = baton;
  svn_wc_notify_t *inner_notify = svn_wc_dup_notify(notify, pool);
  const char *relpath;

  relpath = svn_dirent_skip_ancestor(nb->checkout_abspath, notify->path);
  inner_notify->path = svn_dirent_join(nb->final_abspath, relpath, pool);

  if (nb->inner_func)
    nb->inner_func(nb->inner_baton, inner_notify, pool);
}

/* Peform each individual copy operation for a repos -> wc copy.  A
   helper for repos_to_wc_copy().

   Resolve PAIR->src_revnum to a real revision number if it isn't already. */
static svn_error_t *
repos_to_wc_copy_single(svn_client__copy_pair_t *pair,
                        svn_boolean_t same_repositories,
                        svn_boolean_t ignore_externals,
                        svn_ra_session_t *ra_session,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *pool)
{
  apr_hash_t *src_mergeinfo;
  const char *dst_abspath = pair->dst_abspath_or_url;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(dst_abspath));

  if (pair->src_kind == svn_node_dir)
    {
      svn_boolean_t sleep_needed = FALSE;
      const char *tmp_abspath;

      /* Find a temporary location in which to check out the copy source.
       * (This function is deprecated, but we intend to replace this whole
       * code path with something else.) */
      SVN_ERR(svn_wc_create_tmp_file2(NULL, &tmp_abspath, dst_abspath,
                                      svn_io_file_del_on_close, pool));

      /* Make a new checkout of the requested source. While doing so,
       * resolve pair->src_revnum to an actual revision number in case it
       * was until now 'invalid' meaning 'head'.  Ask this function not to
       * sleep for timestamps, by passing a sleep_needed output param.
       * Send notifications for all nodes except the root node, and adjust
       * them to refer to the destination rather than this temporary path. */
      {
        svn_wc_notify_func2_t old_notify_func2 = ctx->notify_func2;
        void *old_notify_baton2 = ctx->notify_baton2;
        struct notification_adjust_baton nb;
        svn_error_t *err;

        nb.inner_func = ctx->notify_func2;
        nb.inner_baton = ctx->notify_baton2;
        nb.checkout_abspath = tmp_abspath;
        nb.final_abspath = dst_abspath;
        ctx->notify_func2 = notification_adjust_func;
        ctx->notify_baton2 = &nb;

        err = svn_client__checkout_internal(&pair->src_revnum,
                                            pair->src_original,
                                            tmp_abspath,
                                            &pair->src_peg_revision,
                                            &pair->src_op_revision,
                                            svn_depth_infinity,
                                            ignore_externals, FALSE,
                                            &sleep_needed, ctx, pool);

        ctx->notify_func2 = old_notify_func2;
        ctx->notify_baton2 = old_notify_baton2;

        SVN_ERR(err);
      }

      /* Schedule dst_path for addition in parent, with copy history.
         Don't send any notification here.
         Then remove the temporary checkout's .svn dir in preparation for
         moving the rest of it into the final destination. */
      if (same_repositories)
        {
          SVN_ERR(svn_wc_copy3(ctx->wc_ctx, tmp_abspath, dst_abspath,
                               TRUE /* metadata_only */,
                               ctx->cancel_func, ctx->cancel_baton,
                               NULL, NULL, pool));
          SVN_ERR(svn_wc__acquire_write_lock(NULL, ctx->wc_ctx, tmp_abspath,
                                             FALSE, pool, pool));
          SVN_ERR(svn_wc_remove_from_revision_control2(ctx->wc_ctx,
                                                       tmp_abspath,
                                                       FALSE, FALSE,
                                                       ctx->cancel_func,
                                                       ctx->cancel_baton,
                                                       pool));

          /* Move the temporary disk tree into place. */
          SVN_ERR(svn_io_file_rename(tmp_abspath, dst_abspath, pool));
        }
      else
        {
          /* ### We want to schedule this as a simple add, or even better
             a copy from a foreign repos, but we don't yet have the
             WC APIs to do that, so we will just move the whole WC into
             place as a disjoint, nested WC. */

          /* Move the working copy to where it is expected. */
          SVN_ERR(svn_wc__rename_wc(ctx->wc_ctx, tmp_abspath, dst_abspath,
                                    pool));

          svn_io_sleep_for_timestamps(dst_abspath, pool);

          return svn_error_createf(
             SVN_ERR_UNSUPPORTED_FEATURE, NULL,
             _("Source URL '%s' is from foreign repository; "
               "leaving it as a disjoint WC"), pair->src_abspath_or_url);
        }
    } /* end directory case */

  else if (pair->src_kind == svn_node_file)
    {
      apr_hash_t *new_props;
      const char *src_rel;
      svn_stream_t *new_base_contents = svn_stream_buffered(pool);

      SVN_ERR(svn_ra_get_path_relative_to_session(ra_session, &src_rel,
                                                  pair->src_abspath_or_url,
                                                  pool));
      /* Fetch the file content. While doing so, resolve pair->src_revnum
       * to an actual revision number if it's 'invalid' meaning 'head'. */
      SVN_ERR(svn_ra_get_file(ra_session, src_rel, pair->src_revnum,
                              new_base_contents,
                              &pair->src_revnum, &new_props, pool));
      SVN_ERR(svn_wc_add_repos_file4(
         ctx->wc_ctx, dst_abspath,
         new_base_contents, NULL, new_props, NULL,
         same_repositories ? pair->src_abspath_or_url : NULL,
         same_repositories ? pair->src_revnum : SVN_INVALID_REVNUM,
         ctx->cancel_func, ctx->cancel_baton,
         pool));
    }

  /* Record the implied mergeinfo (before the notification callback
     is invoked for the root node). */
  SVN_ERR(svn_client__get_repos_mergeinfo(
            &src_mergeinfo, ra_session,
            pair->src_abspath_or_url, pair->src_revnum,
            svn_mergeinfo_inherited, TRUE /*squelch_incapable*/, pool));
  SVN_ERR(extend_wc_mergeinfo(dst_abspath, src_mergeinfo, ctx, pool));

  /* Do our own notification for the root node, even if we could possibly
     have delegated it.  See also issue #1552.

     ### Maybe this notification should mention the mergeinfo change. */
  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(
                                  dst_abspath, svn_wc_notify_add, pool);
      notify->kind = pair->src_kind;
      (*ctx->notify_func2)(ctx->notify_baton2, notify, pool);
    }

  svn_io_sleep_for_timestamps(dst_abspath, pool);

  return SVN_NO_ERROR;
}

static svn_error_t*
repos_to_wc_copy_locked(const apr_array_header_t *copy_pairs,
                        const char *top_dst_path,
                        svn_boolean_t ignore_externals,
                        svn_ra_session_t *ra_session,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *scratch_pool)
{
  int i;
  const char *src_uuid = NULL, *dst_uuid = NULL;
  svn_boolean_t same_repositories;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* We've already checked for physical obstruction by a working file.
     But there could also be logical obstruction by an entry whose
     working file happens to be missing.*/
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *);
      svn_node_kind_t kind;
      svn_boolean_t is_excluded;
      svn_boolean_t is_server_excluded;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_wc_read_kind(&kind, ctx->wc_ctx, pair->dst_abspath_or_url,
                               FALSE, iterpool));
      if (kind == svn_node_none)
        continue;

      /* ### TODO(#2843): Rework these error report. Maybe we can
         ### simplify the conditions? */

      /* Hidden by client exclusion */
      SVN_ERR(svn_wc__node_is_status_excluded(&is_excluded, ctx->wc_ctx,
                                              pair->dst_abspath_or_url,
                                              iterpool));
      if (is_excluded)
        {
          return svn_error_createf
            (SVN_ERR_ENTRY_EXISTS,
             NULL, _("'%s' is already under version control"),
             svn_dirent_local_style(pair->dst_abspath_or_url, iterpool));
        }

      /* Hidden by server exclusion (not authorized) */
      SVN_ERR(svn_wc__node_is_status_server_excluded(&is_server_excluded,
                                                     ctx->wc_ctx,
                                                     pair->dst_abspath_or_url,
                                                     iterpool));
      if (is_server_excluded)
        {
          return svn_error_createf
            (SVN_ERR_ENTRY_EXISTS,
             NULL, _("'%s' is already under version control"),
             svn_dirent_local_style(pair->dst_abspath_or_url, iterpool));
        }

      /* Working file missing to something other than being scheduled
         for addition or in "deleted" state. */
      if (kind != svn_node_dir)
        {
          svn_boolean_t is_deleted;
          svn_boolean_t is_not_present;

          SVN_ERR(svn_wc__node_is_status_deleted(&is_deleted, ctx->wc_ctx,
                                                 pair->dst_abspath_or_url,
                                                 iterpool));
          SVN_ERR(svn_wc__node_is_status_not_present(&is_not_present,
                                                     ctx->wc_ctx,
                                                     pair->dst_abspath_or_url,
                                                     iterpool));
          if ((! is_deleted) && (! is_not_present))
            return svn_error_createf
              (SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
               _("Entry for '%s' exists (though the working file is missing)"),
               svn_dirent_local_style(pair->dst_abspath_or_url, iterpool));
        }
    }

  /* Decide whether the two repositories are the same or not. */
  {
    svn_error_t *src_err, *dst_err;
    const char *parent;
    const char *parent_abspath;

    /* Get the repository uuid of SRC_URL */
    src_err = svn_ra_get_uuid2(ra_session, &src_uuid, scratch_pool);
    if (src_err && src_err->apr_err != SVN_ERR_RA_NO_REPOS_UUID)
      return svn_error_trace(src_err);

    /* Get repository uuid of dst's parent directory, since dst may
       not exist.  ### TODO:  we should probably walk up the wc here,
       in case the parent dir has an imaginary URL.  */
    if (copy_pairs->nelts == 1)
      parent = svn_dirent_dirname(top_dst_path, scratch_pool);
    else
      parent = top_dst_path;

    SVN_ERR(svn_dirent_get_absolute(&parent_abspath, parent, scratch_pool));
    dst_err = svn_client_get_repos_root(NULL /* root_url */, &dst_uuid,
                                        parent_abspath, ctx,
                                        scratch_pool, scratch_pool);
    if (dst_err && dst_err->apr_err != SVN_ERR_RA_NO_REPOS_UUID)
      return dst_err;

    /* If either of the UUIDs are nonexistent, then at least one of
       the repositories must be very old.  Rather than punish the
       user, just assume the repositories are different, so no
       copy-history is attempted. */
    if (src_err || dst_err || (! src_uuid) || (! dst_uuid))
      same_repositories = FALSE;

    else
      same_repositories = (strcmp(src_uuid, dst_uuid) == 0);
  }

  /* Perform the move for each of the copy_pairs. */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      /* Check for cancellation */
      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      svn_pool_clear(iterpool);

      SVN_ERR(repos_to_wc_copy_single(APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *),
                                      same_repositories,
                                      ignore_externals,
                                      ra_session, ctx, iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
repos_to_wc_copy(const apr_array_header_t *copy_pairs,
                 svn_boolean_t make_parents,
                 svn_boolean_t ignore_externals,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  const char *top_src_url, *top_dst_path;
  apr_pool_t *iterpool = svn_pool_create(pool);
  const char *lock_abspath;
  int i;

  /* Get the real path for the source, based upon its peg revision. */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *);
      const char *src;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_client__repos_locations(&src, &pair->src_revnum, NULL, NULL,
                                          NULL,
                                          pair->src_abspath_or_url,
                                          &pair->src_peg_revision,
                                          &pair->src_op_revision, NULL,
                                          ctx, iterpool));

      pair->src_original = pair->src_abspath_or_url;
      pair->src_abspath_or_url = apr_pstrdup(pool, src);
    }

  SVN_ERR(get_copy_pair_ancestors(copy_pairs, &top_src_url, &top_dst_path,
                                  NULL, pool));
  lock_abspath = top_dst_path;
  if (copy_pairs->nelts == 1)
    {
      top_src_url = svn_uri_dirname(top_src_url, pool);
      lock_abspath = svn_dirent_dirname(top_dst_path, pool);
    }

  /* Open a repository session to the longest common src ancestor.  We do not
     (yet) have a working copy, so we don't have a corresponding path and
     tempfiles cannot go into the admin area. */
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, NULL, top_src_url,
                                               NULL, NULL, FALSE, TRUE,
                                               ctx, pool));

  /* Get the correct src path for the peg revision used, and verify that we
     aren't overwriting an existing path. */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *);
      svn_node_kind_t dst_parent_kind, dst_kind;
      const char *dst_parent;
      const char *src_rel;

      svn_pool_clear(iterpool);

      /* Next, make sure that the path exists in the repository. */
      SVN_ERR(svn_ra_get_path_relative_to_session(ra_session, &src_rel,
                                                  pair->src_abspath_or_url,
                                                  iterpool));
      SVN_ERR(svn_ra_check_path(ra_session, src_rel, pair->src_revnum,
                                &pair->src_kind, pool));
      if (pair->src_kind == svn_node_none)
        {
          if (SVN_IS_VALID_REVNUM(pair->src_revnum))
            return svn_error_createf
              (SVN_ERR_FS_NOT_FOUND, NULL,
               _("Path '%s' not found in revision %ld"),
               pair->src_abspath_or_url, pair->src_revnum);
          else
            return svn_error_createf
              (SVN_ERR_FS_NOT_FOUND, NULL,
               _("Path '%s' not found in head revision"),
               pair->src_abspath_or_url);
        }

      /* Figure out about dst. */
      SVN_ERR(svn_io_check_path(pair->dst_abspath_or_url, &dst_kind,
                                iterpool));
      if (dst_kind != svn_node_none)
        {
          return svn_error_createf(
            SVN_ERR_ENTRY_EXISTS, NULL,
            _("Path '%s' already exists"),
            svn_dirent_local_style(pair->dst_abspath_or_url, pool));
        }

      /* Make sure the destination parent is a directory and produce a clear
         error message if it is not. */
      dst_parent = svn_dirent_dirname(pair->dst_abspath_or_url, iterpool);
      SVN_ERR(svn_io_check_path(dst_parent, &dst_parent_kind, iterpool));
      if (make_parents && dst_parent_kind == svn_node_none)
        {
          SVN_ERR(svn_client__make_local_parents(dst_parent, TRUE, ctx,
                                                 iterpool));
        }
      else if (dst_parent_kind != svn_node_dir)
        {
          return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                                   _("Path '%s' is not a directory"),
                                   svn_dirent_local_style(dst_parent, pool));
        }
    }
  svn_pool_destroy(iterpool);

  SVN_WC__CALL_WITH_WRITE_LOCK(
    repos_to_wc_copy_locked(copy_pairs, top_dst_path, ignore_externals,
                            ra_session, ctx, pool),
    ctx->wc_ctx, lock_abspath, FALSE, pool);
  return SVN_NO_ERROR;
}

#define NEED_REPOS_REVNUM(revision) \
        ((revision.kind != svn_opt_revision_unspecified) \
          && (revision.kind != svn_opt_revision_working))

/* Perform all allocations in POOL. */
static svn_error_t *
try_copy(const apr_array_header_t *sources,
         const char *dst_path_in,
         svn_boolean_t is_move,
         svn_boolean_t make_parents,
         svn_boolean_t ignore_externals,
         const apr_hash_t *revprop_table,
         svn_commit_callback2_t commit_callback,
         void *commit_baton,
         svn_client_ctx_t *ctx,
         apr_pool_t *pool)
{
  apr_array_header_t *copy_pairs =
                        apr_array_make(pool, sources->nelts,
                                       sizeof(svn_client__copy_pair_t *));
  svn_boolean_t srcs_are_urls, dst_is_url;
  int i;

  /* Are either of our paths URLs?  Just check the first src_path.  If
     there are more than one, we'll check for homogeneity among them
     down below. */
  srcs_are_urls = svn_path_is_url(APR_ARRAY_IDX(sources, 0,
                                  svn_client_copy_source_t *)->path);
  dst_is_url = svn_path_is_url(dst_path_in);
  if (!dst_is_url)
    SVN_ERR(svn_dirent_get_absolute(&dst_path_in, dst_path_in, pool));

  /* If we have multiple source paths, it implies the dst_path is a
     directory we are moving or copying into.  Populate the COPY_PAIRS
     array to contain a destination path for each of the source paths. */
  if (sources->nelts > 1)
    {
      apr_pool_t *iterpool = svn_pool_create(pool);

      for (i = 0; i < sources->nelts; i++)
        {
          svn_client_copy_source_t *source = APR_ARRAY_IDX(sources, i,
                                               svn_client_copy_source_t *);
          svn_client__copy_pair_t *pair = apr_palloc(pool, sizeof(*pair));
          const char *src_basename;
          svn_boolean_t src_is_url = svn_path_is_url(source->path);

          svn_pool_clear(iterpool);

          if (src_is_url)
            {
              pair->src_abspath_or_url = apr_pstrdup(pool, source->path);
              src_basename = svn_uri_basename(pair->src_abspath_or_url,
                                              iterpool);
            }
          else
            {
              SVN_ERR(svn_dirent_get_absolute(&pair->src_abspath_or_url,
                                              source->path, pool));
              src_basename = svn_dirent_basename(pair->src_abspath_or_url,
                                                 iterpool);
            }

          pair->src_op_revision = *source->revision;
          pair->src_peg_revision = *source->peg_revision;

          SVN_ERR(svn_opt_resolve_revisions(&pair->src_peg_revision,
                                            &pair->src_op_revision,
                                            src_is_url,
                                            TRUE,
                                            iterpool));

          /* Check to see if all the sources are urls or all working copy
           * paths. */
          if (src_is_url != srcs_are_urls)
            return svn_error_create
              (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
               _("Cannot mix repository and working copy sources"));

          if (dst_is_url)
            pair->dst_abspath_or_url =
              svn_path_url_add_component2(dst_path_in, src_basename, pool);
          else
            pair->dst_abspath_or_url = svn_dirent_join(dst_path_in,
                                                       src_basename, pool);
          APR_ARRAY_PUSH(copy_pairs, svn_client__copy_pair_t *) = pair;
        }

      svn_pool_destroy(iterpool);
    }
  else
    {
      /* Only one source path. */
      svn_client__copy_pair_t *pair = apr_palloc(pool, sizeof(*pair));
      svn_client_copy_source_t *source =
        APR_ARRAY_IDX(sources, 0, svn_client_copy_source_t *);
      svn_boolean_t src_is_url = svn_path_is_url(source->path);

      if (src_is_url)
        pair->src_abspath_or_url = apr_pstrdup(pool, source->path);
      else
        SVN_ERR(svn_dirent_get_absolute(&pair->src_abspath_or_url,
                                        source->path, pool));
      pair->src_op_revision = *source->revision;
      pair->src_peg_revision = *source->peg_revision;

      SVN_ERR(svn_opt_resolve_revisions(&pair->src_peg_revision,
                                        &pair->src_op_revision,
                                        src_is_url, TRUE, pool));

      pair->dst_abspath_or_url = dst_path_in;
      APR_ARRAY_PUSH(copy_pairs, svn_client__copy_pair_t *) = pair;
    }

  if (!srcs_are_urls && !dst_is_url)
    {
      apr_pool_t *iterpool = svn_pool_create(pool);

      for (i = 0; i < copy_pairs->nelts; i++)
        {
          svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                            svn_client__copy_pair_t *);

          svn_pool_clear(iterpool);

          if (svn_dirent_is_child(pair->src_abspath_or_url,
                                  pair->dst_abspath_or_url, iterpool))
            return svn_error_createf
              (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
               _("Cannot copy path '%s' into its own child '%s'"),
               svn_dirent_local_style(pair->src_abspath_or_url, pool),
               svn_dirent_local_style(pair->dst_abspath_or_url, pool));
        }

      svn_pool_destroy(iterpool);
    }

  /* A file external should not be moved since the file external is
     implemented as a switched file and it would delete the file the
     file external is switched to, which is not the behavior the user
     would probably want. */
  if (is_move && !srcs_are_urls)
    {
      apr_pool_t *iterpool = svn_pool_create(pool);

      for (i = 0; i < copy_pairs->nelts; i++)
        {
          svn_client__copy_pair_t *pair =
            APR_ARRAY_IDX(copy_pairs, i, svn_client__copy_pair_t *);
          svn_node_kind_t external_kind;
          const char *defining_abspath;

          svn_pool_clear(iterpool);

          SVN_ERR_ASSERT(svn_dirent_is_absolute(pair->src_abspath_or_url));
          SVN_ERR(svn_wc__read_external_info(&external_kind, &defining_abspath,
                                             NULL, NULL, NULL, ctx->wc_ctx,
                                             pair->src_abspath_or_url,
                                             pair->src_abspath_or_url, TRUE,
                                             iterpool, iterpool));

          if (external_kind != svn_node_none)
            return svn_error_createf(
                     SVN_ERR_WC_CANNOT_MOVE_FILE_EXTERNAL,
                     NULL,
                     _("Cannot move the external at '%s'; please "
                       "edit the svn:externals property on '%s'."),
                     svn_dirent_local_style(pair->src_abspath_or_url, pool),
                     svn_dirent_local_style(defining_abspath, pool));
        }
      svn_pool_destroy(iterpool);
    }

  if (is_move)
    {
      /* Disallow moves between the working copy and the repository. */
      if (srcs_are_urls != dst_is_url)
        {
          return svn_error_create
            (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
             _("Moves between the working copy and the repository are not "
               "supported"));
        }

      /* Disallow moving any path/URL onto or into itself. */
      for (i = 0; i < copy_pairs->nelts; i++)
        {
          svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                            svn_client__copy_pair_t *);

          if (strcmp(pair->src_abspath_or_url,
                     pair->dst_abspath_or_url) == 0)
            return svn_error_createf(
              SVN_ERR_UNSUPPORTED_FEATURE, NULL,
              srcs_are_urls ?
                _("Cannot move URL '%s' into itself") :
                _("Cannot move path '%s' into itself"),
              srcs_are_urls ?
                pair->src_abspath_or_url :
                svn_dirent_local_style(pair->src_abspath_or_url, pool));
        }
    }
  else
    {
      if (!srcs_are_urls)
        {
          /* If we are doing a wc->* copy, but with an operational revision
             other than the working copy revision, we are really doing a
             repo->* copy, because we're going to need to get the rev from the
             repo. */

          svn_boolean_t need_repos_op_rev = FALSE;
          svn_boolean_t need_repos_peg_rev = FALSE;

          /* Check to see if any revision is something other than
             svn_opt_revision_unspecified or svn_opt_revision_working. */
          for (i = 0; i < copy_pairs->nelts; i++)
            {
              svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                svn_client__copy_pair_t *);

              if (NEED_REPOS_REVNUM(pair->src_op_revision))
                need_repos_op_rev = TRUE;

              if (NEED_REPOS_REVNUM(pair->src_peg_revision))
                need_repos_peg_rev = TRUE;

              if (need_repos_op_rev || need_repos_peg_rev)
                break;
            }

          if (need_repos_op_rev || need_repos_peg_rev)
            {
              apr_pool_t *iterpool = svn_pool_create(pool);

              for (i = 0; i < copy_pairs->nelts; i++)
                {
                  const char *copyfrom_repos_root_url;
                  const char *copyfrom_repos_relpath;
                  const char *url;
                  svn_revnum_t copyfrom_rev;
                  svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *);

                  svn_pool_clear(iterpool);

                  SVN_ERR_ASSERT(svn_dirent_is_absolute(pair->src_abspath_or_url));

                  SVN_ERR(svn_wc__node_get_origin(NULL, &copyfrom_rev,
                                                  &copyfrom_repos_relpath,
                                                  &copyfrom_repos_root_url,
                                                  NULL, NULL,
                                                  ctx->wc_ctx,
                                                  pair->src_abspath_or_url,
                                                  TRUE, iterpool, iterpool));

                  if (copyfrom_repos_relpath)
                    url = svn_path_url_add_component2(copyfrom_repos_root_url,
                                                      copyfrom_repos_relpath,
                                                      pool);
                  else
                    return svn_error_createf
                      (SVN_ERR_ENTRY_MISSING_URL, NULL,
                       _("'%s' does not have a URL associated with it"),
                       svn_dirent_local_style(pair->src_abspath_or_url, pool));

                  pair->src_abspath_or_url = url;

                  if (!need_repos_peg_rev
                      || pair->src_peg_revision.kind == svn_opt_revision_base)
                    {
                      /* Default the peg revision to that of the WC entry. */
                      pair->src_peg_revision.kind = svn_opt_revision_number;
                      pair->src_peg_revision.value.number = copyfrom_rev;
                    }

                  if (pair->src_op_revision.kind == svn_opt_revision_base)
                    {
                      /* Use the entry's revision as the operational rev. */
                      pair->src_op_revision.kind = svn_opt_revision_number;
                      pair->src_op_revision.value.number = copyfrom_rev;
                    }
                }

              svn_pool_destroy(iterpool);
              srcs_are_urls = TRUE;
            }
        }
    }

  /* Now, call the right handler for the operation. */
  if ((! srcs_are_urls) && (! dst_is_url))
    {
      SVN_ERR(verify_wc_srcs_and_dsts(copy_pairs, make_parents, is_move,
                                      ctx, pool));

      /* Copy or move all targets. */
      if (is_move)
        return svn_error_trace(do_wc_to_wc_moves(copy_pairs, dst_path_in, ctx,
                                                 pool));
      else
        return svn_error_trace(do_wc_to_wc_copies(copy_pairs, ctx, pool));
    }
  else if ((! srcs_are_urls) && (dst_is_url))
    {
      return svn_error_trace(
        wc_to_repos_copy(copy_pairs, make_parents, revprop_table,
                         commit_callback, commit_baton, ctx, pool));
    }
  else if ((srcs_are_urls) && (! dst_is_url))
    {
      return svn_error_trace(
        repos_to_wc_copy(copy_pairs, make_parents, ignore_externals,
                         ctx, pool));
    }
  else
    {
      return svn_error_trace(
        repos_to_repos_copy(copy_pairs, make_parents, revprop_table,
                            commit_callback, commit_baton, ctx, is_move,
                            pool));
    }
}



/* Public Interfaces */
svn_error_t *
svn_client_copy6(const apr_array_header_t *sources,
                 const char *dst_path,
                 svn_boolean_t copy_as_child,
                 svn_boolean_t make_parents,
                 svn_boolean_t ignore_externals,
                 const apr_hash_t *revprop_table,
                 svn_commit_callback2_t commit_callback,
                 void *commit_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_error_t *err;
  apr_pool_t *subpool = svn_pool_create(pool);

  if (sources->nelts > 1 && !copy_as_child)
    return svn_error_create(SVN_ERR_CLIENT_MULTIPLE_SOURCES_DISALLOWED,
                            NULL, NULL);

  err = try_copy(sources, dst_path,
                 FALSE /* is_move */,
                 make_parents,
                 ignore_externals,
                 revprop_table,
                 commit_callback, commit_baton,
                 ctx,
                 subpool);

  /* If the destination exists, try to copy the sources as children of the
     destination. */
  if (copy_as_child && err && (sources->nelts == 1)
        && (err->apr_err == SVN_ERR_ENTRY_EXISTS
            || err->apr_err == SVN_ERR_FS_ALREADY_EXISTS))
    {
      const char *src_path = APR_ARRAY_IDX(sources, 0,
                                           svn_client_copy_source_t *)->path;
      const char *src_basename;
      svn_boolean_t src_is_url = svn_path_is_url(src_path);
      svn_boolean_t dst_is_url = svn_path_is_url(dst_path);

      svn_error_clear(err);
      svn_pool_clear(subpool);

      src_basename = src_is_url ? svn_uri_basename(src_path, subpool)
                                : svn_dirent_basename(src_path, subpool);

      err = try_copy(sources,
                     dst_is_url
                         ? svn_path_url_add_component2(dst_path, src_basename,
                                                       subpool)
                         : svn_dirent_join(dst_path, src_basename, subpool),
                     FALSE /* is_move */,
                     make_parents,
                     ignore_externals,
                     revprop_table,
                     commit_callback, commit_baton,
                     ctx,
                     subpool);
    }

  svn_pool_destroy(subpool);
  return svn_error_trace(err);
}


svn_error_t *
svn_client_move6(const apr_array_header_t *src_paths,
                 const char *dst_path,
                 svn_boolean_t move_as_child,
                 svn_boolean_t make_parents,
                 const apr_hash_t *revprop_table,
                 svn_commit_callback2_t commit_callback,
                 void *commit_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  const svn_opt_revision_t head_revision
    = { svn_opt_revision_head, { 0 } };
  svn_error_t *err;
  int i;
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_array_header_t *sources = apr_array_make(pool, src_paths->nelts,
                                  sizeof(const svn_client_copy_source_t *));

  if (src_paths->nelts > 1 && !move_as_child)
    return svn_error_create(SVN_ERR_CLIENT_MULTIPLE_SOURCES_DISALLOWED,
                            NULL, NULL);

  for (i = 0; i < src_paths->nelts; i++)
    {
      const char *src_path = APR_ARRAY_IDX(src_paths, i, const char *);
      svn_client_copy_source_t *copy_source = apr_palloc(pool,
                                                         sizeof(*copy_source));

      copy_source->path = src_path;
      copy_source->revision = &head_revision;
      copy_source->peg_revision = &head_revision;

      APR_ARRAY_PUSH(sources, svn_client_copy_source_t *) = copy_source;
    }

  err = try_copy(sources, dst_path,
                 TRUE /* is_move */,
                 make_parents,
                 FALSE,
                 revprop_table,
                 commit_callback, commit_baton,
                 ctx,
                 subpool);

  /* If the destination exists, try to move the sources as children of the
     destination. */
  if (move_as_child && err && (src_paths->nelts == 1)
        && (err->apr_err == SVN_ERR_ENTRY_EXISTS
            || err->apr_err == SVN_ERR_FS_ALREADY_EXISTS))
    {
      const char *src_path = APR_ARRAY_IDX(src_paths, 0, const char *);
      const char *src_basename;
      svn_boolean_t src_is_url = svn_path_is_url(src_path);
      svn_boolean_t dst_is_url = svn_path_is_url(dst_path);

      svn_error_clear(err);
      svn_pool_clear(subpool);

      src_basename = src_is_url ? svn_uri_basename(src_path, pool)
                                : svn_dirent_basename(src_path, pool);

      err = try_copy(sources,
                     dst_is_url
                         ? svn_path_url_add_component2(dst_path,
                                                       src_basename, pool)
                         : svn_dirent_join(dst_path, src_basename, pool),
                     TRUE /* is_move */,
                     make_parents,
                     FALSE,
                     revprop_table,
                     commit_callback, commit_baton,
                     ctx,
                     subpool);
    }

  svn_pool_destroy(subpool);
  return svn_error_trace(err);
}
