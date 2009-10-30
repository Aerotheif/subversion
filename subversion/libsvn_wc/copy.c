/*
 * copy.c:  wc 'copy' functionality.
 *
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
 */

/* ==================================================================== */



/*** Includes. ***/

#include <string.h>
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"

#include "wc.h"
#include "adm_files.h"
#include "entries.h"
#include "props.h"
#include "translate.h"
#include "lock.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"


/*** Code. ***/

/* Copy all properties of SRC_PATH to DST_PATH. */
static svn_error_t *
copy_props(svn_wc__db_t *db,
           const char *src_abspath,
           const char *dst_abspath,
           apr_pool_t *scratch_pool)
{
  apr_hash_t *props;
  apr_hash_index_t *hi;

  SVN_ERR(svn_wc__load_props(NULL, &props, db, src_abspath,
                             scratch_pool, scratch_pool));
  for (hi = apr_hash_first(scratch_pool, props); hi; hi = apr_hash_next(hi))
    {
      const char *propname = svn_apr_hash_index_key(hi);
      svn_string_t *propval = svn_apr_hash_index_val(hi);

      SVN_ERR(svn_wc__internal_propset(db, dst_abspath, propname, propval,
                                       FALSE /* skip_checks */,
                                       NULL, NULL, scratch_pool));
    }

  return SVN_NO_ERROR;
}


/* Helper function for svn_wc_copy2() which handles WC->WC copying of
   files which are scheduled for addition or unversioned.

   Copy file SRC_PATH in SRC_ACCESS to DST_BASENAME in DST_PARENT_ACCESS.

   DST_PARENT_ACCESS is a 0 depth locked access for a versioned directory
   in the same WC as SRC_PATH.

   If SRC_IS_ADDED is true then SRC_PATH is scheduled for addition and
   DST_BASENAME will also be scheduled for addition.

   If SRC_IS_ADDED is false then SRC_PATH is the unversioned child
   file of a versioned or added parent and DST_BASENAME is simply copied.

   Use SCRATCH_POOL for all necessary allocations.
*/
static svn_error_t *
copy_added_file_administratively(svn_wc_context_t *wc_ctx,
                                 const char *src_abspath,
                                 svn_boolean_t src_is_added,
                                 const char *dst_abspath,
                                 svn_cancel_func_t cancel_func,
                                 void *cancel_baton,
                                 svn_wc_notify_func2_t notify_func,
                                 void *notify_baton,
                                 apr_pool_t *scratch_pool)
{
  /* Copy this file and possibly put it under version control. */
  SVN_ERR(svn_io_copy_file(src_abspath, dst_abspath, TRUE, scratch_pool));

  if (src_is_added)
    {
      SVN_ERR(svn_wc_add4(wc_ctx, dst_abspath, svn_depth_infinity, NULL,
                          SVN_INVALID_REVNUM,
                          cancel_func, cancel_baton,
                          notify_func, notify_baton,
                          scratch_pool));

      SVN_ERR(copy_props(wc_ctx->db, src_abspath, dst_abspath, scratch_pool));
    }

  return SVN_NO_ERROR;
}


/* Helper function for svn_wc_copy2() which handles WC->WC copying of
   directories which are scheduled for addition or unversioned.

   Recursively copy directory SRC_ABSPATH and its children, excluding
   administrative directories, to DST_ABSPATH.

   If SRC_IS_ADDED is true then SRC_PATH is scheduled for addition and
   DST_BASENAME will also be scheduled for addition.

   If SRC_IS_ADDED is false then SRC_PATH is the unversioned child
   directory of a versioned or added parent and DST_BASENAME is simply
   copied.

   Use SCRATCH_POOL for all necessary allocations.
*/
static svn_error_t *
copy_added_dir_administratively(svn_wc_context_t *wc_ctx,
                                const char *src_abspath,
                                svn_boolean_t src_is_added,
                                const char *dst_abspath,
                                svn_cancel_func_t cancel_func,
                                void *cancel_baton,
                                svn_wc_notify_func2_t notify_func,
                                void *notify_baton,
                                apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db = wc_ctx->db;

  if (! src_is_added)
    {
      /* src_path is the top of an unversioned tree, just copy
         the whole thing and we are done. */
      SVN_ERR(svn_io_copy_dir_recursively(src_abspath,
                                          svn_dirent_dirname(dst_abspath,
                                                             scratch_pool),
                                          svn_dirent_basename(dst_abspath,
                                                              NULL),
                                          TRUE, cancel_func, cancel_baton,
                                          scratch_pool));
    }
  else
    {
      apr_hash_t *dirents;
      apr_hash_index_t *hi;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);

      /* Check cancellation; note that this catches recursive calls too. */
      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      /* "Copy" the dir dst_path and schedule it, and possibly
         its children, for addition. */
      SVN_ERR(svn_io_dir_make(dst_abspath, APR_OS_DEFAULT, iterpool));

      /* Add the directory */
      SVN_ERR(svn_wc_add4(wc_ctx, dst_abspath, svn_depth_infinity,
                          NULL, SVN_INVALID_REVNUM,
                          cancel_func, cancel_baton,
                          notify_func, notify_baton,
                          iterpool));

      /* Copy properties. */
      SVN_ERR(copy_props(wc_ctx->db, src_abspath, dst_abspath, iterpool));

      SVN_ERR(svn_io_get_dirents2(&dirents, src_abspath, scratch_pool));

      /* Read src_path's entries one by one. */
      for (hi = apr_hash_first(scratch_pool, dirents);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *name = svn_apr_hash_index_key(hi);
          svn_io_dirent_t *dirent = svn_apr_hash_index_val(hi);
          const char *node_abspath;
          svn_wc__db_kind_t kind;

          svn_pool_clear(iterpool);

          /* Check cancellation so you can cancel during an
           * add of a directory with lots of files. */
          if (cancel_func)
            SVN_ERR(cancel_func(cancel_baton));

          /* Skip over SVN admin directories. */
          if (svn_wc_is_adm_dir(name, iterpool))
            continue;

          /* Construct the path of the node. */
          node_abspath = svn_dirent_join(src_abspath, name, iterpool);

          SVN_ERR(svn_wc__db_read_kind(&kind, db, node_abspath, TRUE,
                                       iterpool));

          if (kind != svn_wc__db_kind_unknown)
            {
              svn_boolean_t hidden;

              SVN_ERR(svn_wc__db_node_hidden(&hidden, db, node_abspath,
                                             iterpool));

              if (hidden)
                kind = svn_wc__db_kind_unknown;
            }

          /* We do not need to handle excluded items here, since this function
             only deal with the sources which are not yet in the repos.
             Exclude flag is by definition not expected in such situation. */

          /* Recurse on directories; add files; ignore the rest. */
          if (dirent->kind == svn_node_dir)
            {
              SVN_ERR(copy_added_dir_administratively(
                                       wc_ctx, node_abspath,
                                       (kind != svn_wc__db_kind_unknown),
                                       svn_dirent_join(dst_abspath, name,
                                                       iterpool),
                                       cancel_func, cancel_baton,
                                       notify_func, notify_baton,
                                       iterpool));
            }
          else if (dirent->kind == svn_node_file)
            {
              SVN_ERR(copy_added_file_administratively(
                                       wc_ctx, node_abspath,
                                       (kind != svn_wc__db_kind_unknown),
                                       svn_dirent_join(dst_abspath, name,
                                                       iterpool),
                                       cancel_func, cancel_baton,
                                       notify_func, notify_baton,
                                       iterpool));
            }

        }

      svn_pool_destroy(iterpool);

    } /* End else src_is_added. */

  return SVN_NO_ERROR;
}

/* A helper for copy_file_administratively() which sets *COPYFROM_URL
   and *COPYFROM_REV appropriately (possibly to NULL/SVN_INVALID_REVNUM).
   DST_URL may be NULL, in which case DST_REVISION is ignored. */
static svn_error_t *
determine_copyfrom_info(const char **copyfrom_url,
                        svn_revnum_t *copyfrom_rev,
                        svn_wc__db_t *db,
                        const char *src_abspath,
                        const char *dst_url,
                        svn_revnum_t dst_revision,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  const char *url;
  svn_revnum_t rev;
  const char *src_copyfrom_root_url;
  const char *src_copyfrom_relpath;
  svn_revnum_t src_copyfrom_rev;
  svn_wc__db_status_t status;

  SVN_ERR(svn_wc__db_read_info(&status, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, &src_copyfrom_relpath,
                               &src_copyfrom_root_url, NULL,
                               &src_copyfrom_rev, NULL, NULL, NULL, NULL,
                               NULL, db, src_abspath, result_pool,
                               scratch_pool));

  if (src_copyfrom_root_url && src_copyfrom_relpath)
    {
      /* When copying/moving a file that was already explicitly
         copied/moved then we know the URL it was copied from... */
      url = svn_path_url_add_component2(src_copyfrom_root_url,
                                        src_copyfrom_relpath, result_pool);
      rev = src_copyfrom_rev;
    }
  else if (status == svn_wc__db_status_added
           || status == svn_wc__db_status_obstructed_add)
    {
      /* ...But if this file is merely the descendant of an explicitly
         copied/moved directory, we need to do a bit more work to
         determine copyfrom_url and copyfrom_rev. */
      const char *op_root_abspath;
      const char *original_repos_relpath;
      const char *original_root_url;
      svn_revnum_t original_revision;
      const char *op_root_rel_path;

      SVN_ERR(svn_wc__db_scan_addition(&status, &op_root_abspath,
                                       NULL, NULL, NULL,
                                       &original_repos_relpath,
                                       &original_root_url, NULL,
                                       &original_revision,
                                       db, src_abspath,
                                       scratch_pool, scratch_pool));
      url = svn_uri_join(original_root_url, original_repos_relpath,
                         result_pool);
      op_root_rel_path = svn_dirent_is_child(op_root_abspath, src_abspath,
                                             scratch_pool);
      url = svn_uri_join(url, op_root_rel_path, scratch_pool);
      rev = original_revision;
    }
  else
    {
      url = NULL;
      rev = SVN_INVALID_REVNUM;
    }

  if (url && dst_url && strcmp(url, dst_url) == 0 && rev == dst_revision)
    {
      /* Suppress copyfrom info when the copy source is the same as
         for the destination. */
      url = NULL;
      rev = SVN_INVALID_REVNUM;
    }

  if (url)
    *copyfrom_url = apr_pstrdup(result_pool, url);
  else
    *copyfrom_url = NULL;
  *copyfrom_rev = rev;

  return SVN_NO_ERROR;
}

/* This function effectively creates and schedules a file for
   addition, but does extra administrative things to allow it to
   function as a 'copy'.

   ASSUMPTIONS:

     - src_abspath is under version control; the working file doesn't
                  necessarily exist (its text-base does).
     - dst_abspath will be the 'new' name of the copied file.
 */
static svn_error_t *
copy_file_administratively(svn_wc_context_t *wc_ctx,
                           const char *src_abspath,
                           const char *dst_abspath,
                           svn_cancel_func_t cancel_func,
                           void *cancel_baton,
                           svn_wc_notify_func2_t notify_func,
                           void *notify_baton,
                           apr_pool_t *scratch_pool)
{
  svn_node_kind_t dst_kind;
  const svn_wc_entry_t *src_entry, *dst_entry;
  svn_wc__db_t *db = wc_ctx->db;
  svn_error_t *err;

  /* Sanity check:  if dst file exists already, don't allow overwrite. */
  SVN_ERR(svn_io_check_path(dst_abspath, &dst_kind, scratch_pool));
  if (dst_kind != svn_node_none)
    return svn_error_createf(SVN_ERR_ENTRY_EXISTS, NULL,
                             _("'%s' already exists and is in the way"),
                             svn_dirent_local_style(dst_abspath,
                                                    scratch_pool));

  /* Even if DST_ABSPATH doesn't exist it may still be a versioned item; it
     may be scheduled for deletion, or the user may simply have removed the
     working copy.  Since we are going to write to DST_PATH text-base and
     prop-base we need to detect such cases and abort. */
  err = svn_wc__get_entry(&dst_entry, db, dst_abspath, TRUE,
                          svn_node_unknown, FALSE,
                          scratch_pool, scratch_pool);

  if (err && err->apr_err == SVN_ERR_NODE_UNEXPECTED_KIND)
    svn_error_clear(err);
  else
    SVN_ERR(err);
  if (dst_entry && dst_entry->schedule != svn_wc_schedule_delete
                && !dst_entry->deleted)
    {
      return svn_error_createf(SVN_ERR_ENTRY_EXISTS, NULL,
                               _("There is already a versioned item '%s'"),
                               svn_dirent_local_style(dst_abspath,
                                                      scratch_pool));
    }

  /* Sanity check 1: You cannot make a copy of something that's not
     under version control. */
  SVN_ERR(svn_wc__get_entry(&src_entry, db, src_abspath, FALSE,
                            svn_node_file, FALSE,
                            scratch_pool, scratch_pool));

  /* Sanity check 2: You cannot make a copy of something that's not
     in the repository unless it's a copy of an uncommitted copy. */
  if ((src_entry->schedule == svn_wc_schedule_add && (! src_entry->copied))
      || (! src_entry->url))
    return svn_error_createf
      (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
       _("Cannot copy or move '%s': it is not in the repository yet; "
         "try committing first"),
       svn_dirent_local_style(src_abspath, scratch_pool));


  /* Schedule the new file for addition in its parent, WITH HISTORY. */
  {
    const char *copyfrom_url;
    svn_revnum_t copyfrom_rev;
    apr_hash_t *props, *base_props;
    svn_stream_t *base_contents;
    svn_stream_t *contents;
    svn_boolean_t src_exists;

    src_exists = TRUE;

    /* Are we moving or copying a file that is already moved or copied
       but not committed? */
    if (src_entry->copied)
      {
        SVN_ERR(determine_copyfrom_info(&copyfrom_url, &copyfrom_rev, db,
                                        src_abspath,
                                        dst_entry ? dst_entry->url : NULL,
                                        dst_entry ? dst_entry->revision
                                                  : SVN_INVALID_REVNUM,
                                        scratch_pool, scratch_pool));
      }
    else
      {
        SVN_ERR(svn_wc__internal_get_ancestry(&copyfrom_url, &copyfrom_rev,
                                              db, src_abspath,
                                              scratch_pool, scratch_pool));
      }

    /* Load source base and working props. */
    SVN_ERR(svn_wc__load_props(&base_props, &props, db,
                               src_abspath, scratch_pool, scratch_pool));

    /* Copy working copy file to temporary location */
    {
      svn_boolean_t special;

      SVN_ERR(svn_wc__get_special(&special, db, src_abspath, scratch_pool));
      if (special)
        {
          SVN_ERR(svn_subst_read_specialfile(&contents, src_abspath,
                                             scratch_pool, scratch_pool));
        }
      else
        {
          svn_subst_eol_style_t eol_style;
          const char *eol_str;
          apr_hash_t *keywords;

          SVN_ERR(svn_wc__get_keywords(&keywords, db, src_abspath, NULL,
                                       scratch_pool, scratch_pool));
          SVN_ERR(svn_wc__get_eol_style(&eol_style, &eol_str, db,
                                        src_abspath,
                                        scratch_pool, scratch_pool));

          /* Try with the working file and fallback on its text-base. */
          err = svn_stream_open_readonly(&contents, src_abspath,
                                         scratch_pool, scratch_pool);
          if (err && APR_STATUS_IS_ENOENT(err->apr_err))
            {
              svn_error_clear(err);

              src_exists = FALSE;

              err = svn_wc__get_pristine_contents(&contents, db, src_abspath,
                                                  scratch_pool, scratch_pool);

              if (err && APR_STATUS_IS_ENOENT(err->apr_err))
                return svn_error_create(SVN_ERR_WC_COPYFROM_PATH_NOT_FOUND,
                                        err, NULL);
              else if (err)
                return svn_error_return(err);
            }
          else if (err)
            return svn_error_return(err);

          if (svn_subst_translation_required(eol_style, eol_str, keywords,
                                             FALSE, FALSE))
            {
              svn_boolean_t repair = FALSE;

              if (eol_style == svn_subst_eol_style_native)
                eol_str = SVN_SUBST_NATIVE_EOL_STR;
              else if (eol_style == svn_subst_eol_style_fixed)
                repair = TRUE;
              else if (eol_style != svn_subst_eol_style_none)
                return svn_error_create(SVN_ERR_IO_UNKNOWN_EOL, NULL, NULL);

              /* Wrap the stream to translate to normal form */
              contents = svn_subst_stream_translated(contents,
                                                     eol_str,
                                                     repair,
                                                     keywords,
                                                     FALSE /* expand */,
                                                     scratch_pool);
            }
        }
    }

    SVN_ERR(svn_wc_get_pristine_contents2(&base_contents, wc_ctx, src_abspath,
                                          scratch_pool, scratch_pool));

    SVN_ERR(svn_wc_add_repos_file4(wc_ctx, dst_abspath,
                                   base_contents, contents,
                                   base_props, props,
                                   copyfrom_url, copyfrom_rev,
                                   cancel_func, cancel_baton,
                                   notify_func, notify_baton,
                                   scratch_pool));

    SVN_ERR(svn_io_copy_perms(src_abspath, dst_abspath, scratch_pool));
  }

  /* Report the addition to the caller. */
  if (notify_func != NULL)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(dst_abspath,
                                                     svn_wc_notify_add,
                                                     scratch_pool);
      notify->kind = svn_node_file;
      (*notify_func)(notify_baton, notify, scratch_pool);
    }

  return SVN_NO_ERROR;
}


/* Recursively crawl over a directory PATH and do a number of things:
     - Remove lock tokens
     - Remove the DAV cache
     - Convert deleted items to schedule-delete items
     - Set .svn directories to be hidden
*/
static svn_error_t *
post_copy_cleanup(svn_wc__db_t *db,
                  const char *local_abspath,
                  apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  const apr_array_header_t *children;
  int i;

  /* Clear the DAV cache.  */
  SVN_ERR(svn_wc__db_base_set_dav_cache(db, local_abspath, NULL,
                                        scratch_pool));

  /* Because svn_io_copy_dir_recursively() doesn't copy directory
     permissions, we'll patch up our tree's .svn subdirs to be
     hidden. */
#ifdef APR_FILE_ATTR_HIDDEN
  {
    const char *adm_dir = svn_wc__adm_child(local_abspath, NULL,
                                            scratch_pool);
    const char *path_apr;
    apr_status_t status;

    SVN_ERR(svn_path_cstring_from_utf8(&path_apr, adm_dir, scratch_pool));
    status = apr_file_attrs_set(path_apr,
                                APR_FILE_ATTR_HIDDEN,
                                APR_FILE_ATTR_HIDDEN,
                                scratch_pool);
    if (status)
      return svn_error_wrap_apr(status, _("Can't hide directory '%s'"),
                                svn_dirent_local_style(adm_dir,
                                                       scratch_pool));
  }
#endif

  /* Loop over all children, removing lock tokens and recursing into
     directories. */
  SVN_ERR(svn_wc__db_read_children(&children, db, local_abspath,
                                   scratch_pool, scratch_pool));
  for (i = 0; i < children->nelts; i++)
    {
      const char *child_basename = APR_ARRAY_IDX(children, i, const char *);
      const char *child_abspath;
      const svn_wc_entry_t *entry;
      svn_wc__db_kind_t kind;

      svn_pool_clear(iterpool);
      child_abspath = svn_dirent_join(local_abspath, child_basename, iterpool);

      SVN_ERR(svn_wc__db_read_kind(&kind, db, child_abspath, FALSE, iterpool));
      SVN_ERR(svn_wc__get_entry(&entry, db, child_abspath, TRUE,
                                svn_node_unknown, (kind == svn_wc__db_kind_dir),
                                iterpool, iterpool));

      if (entry->depth == svn_depth_exclude)
        continue;

      /* Convert deleted="true" into schedule="delete" for all
         children (and grandchildren, if RECURSE is set) of the path.
         The result of this is that when
         the copy is committed the items in question get deleted and
         the result is a directory in the repository that matches the
         original source directory for copy.  If this were not done
         the deleted="true" items would simply vanish from the entries
         file as the copy is added to the working copy.  The new
         schedule="delete" files do not have a text-base and so their
         scheduled deletion cannot be reverted.  For directories a
         placeholder with an svn_node_kind_t of svn_node_file and
         schedule="delete" is used to avoid the problems associated
         with creating a directory.  See Issue #2101 for details. */
      if (entry->deleted)
        {
          apr_uint64_t flags = (SVN_WC__ENTRY_MODIFY_FORCE
                                | SVN_WC__ENTRY_MODIFY_SCHEDULE
                                | SVN_WC__ENTRY_MODIFY_DELETED);
          svn_wc_entry_t tmp_entry;

          tmp_entry.schedule = svn_wc_schedule_delete;
          tmp_entry.deleted = FALSE;

          if (entry->kind == svn_node_dir)
            {
              /* ### WARNING: Very dodgy stuff here! ###

              Directories are a problem since a schedule delete directory
              needs an admin directory to be present.  It's possible to
              create a dummy admin directory and that sort of works, it's
              good enough if the user commits the copy.  Where it falls
              down is if the user *reverts* the dummy directory since the
              now schedule normal, copied, directory doesn't have the
              correct contents.

              The dodgy solution is to cheat and use a schedule delete file
              as a placeholder!  This is sufficient to provide a delete
              when the copy is committed.  Attempts to revert any such
              "fake" files will fail due to a missing text-base. This
              effectively means that the schedule deletes have to remain
              schedule delete until the copy is committed, when they become
              state deleted and everything works! */
              tmp_entry.kind = svn_node_file;
              flags |= SVN_WC__ENTRY_MODIFY_KIND;
            }

          SVN_ERR(svn_wc__entry_modify2(db, child_abspath, svn_node_unknown,
                                        FALSE, &tmp_entry, flags, iterpool));
        }

      /* Remove lock stuffs. */
      if (entry->lock_token)
        SVN_ERR(svn_wc__db_lock_remove(db, local_abspath, iterpool));

      /* If a dir and not deleted, recurse. */
      if (!entry->deleted && entry->kind == svn_node_dir)
        SVN_ERR(post_copy_cleanup(db, child_abspath, iterpool));
    }

  /* Cleanup */
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* This function effectively creates and schedules a dir for
   addition, but does extra administrative things to allow it to
   function as a 'copy'.

   ASSUMPTIONS:

     - src_abspath points to a dir under version control
     - dst_parent is the target of the copy operation. Its parent directory
                  is under version control, in the same working copy.
 */
static svn_error_t *
copy_dir_administratively(svn_wc_context_t *wc_ctx,
                          const char *src_abspath,
                          const char *dst_abspath,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_notify_func2_t notify_copied,
                          void *notify_baton,
                          apr_pool_t *scratch_pool)
{
  const svn_wc_entry_t *src_entry;
  svn_wc__db_t *db = wc_ctx->db;

  /* Sanity check 1: You cannot make a copy of something that's not
     under version control. */
  SVN_ERR(svn_wc__get_entry(&src_entry, db, src_abspath, FALSE,
                            svn_node_dir, FALSE, scratch_pool, scratch_pool));

  /* Sanity check 2: You cannot make a copy of something that's not
     in the repository unless it's a copy of an uncommitted copy. */
  if ((src_entry->schedule == svn_wc_schedule_add && (! src_entry->copied))
      || (! src_entry->url))
    return svn_error_createf
      (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
       _("Cannot copy or move '%s': it is not in the repository yet; "
         "try committing first"),
       svn_dirent_local_style(src_abspath, scratch_pool));

  /* Recursively copy the whole directory over.  This gets us all
     text-base, props, base-props, as well as entries, local mods,
     schedulings, existences, etc.

      ### Should we be copying unversioned items within the directory? */
  SVN_ERR(svn_io_copy_dir_recursively(src_abspath,
                                      svn_dirent_dirname(dst_abspath,
                                                         scratch_pool),
                                      svn_dirent_basename(dst_abspath, NULL),
                                      TRUE,
                                      cancel_func, cancel_baton,
                                      scratch_pool));

  /* If this is part of a move, the copied directory will be locked,
     because the source directory was locked.  Running cleanup will remove
     the locks, even though this directory has not yet been added to the
     parent. */
  SVN_ERR(svn_wc_cleanup3(wc_ctx, dst_abspath, cancel_func, cancel_baton,
                          scratch_pool));

  /* We've got some post-copy cleanup to do now. */
  SVN_ERR(post_copy_cleanup(db, dst_abspath, scratch_pool));

  /* Schedule the directory for addition in both its parent and itself
     (this_dir) -- WITH HISTORY.  This function should leave the
     existing administrative dir untouched.  */
  {
    const char *copyfrom_url;
    svn_revnum_t copyfrom_rev;

    /* Are we copying a dir that is already copied but not committed? */
    if (src_entry->copied)
      {
        const svn_wc_entry_t *dst_entry;
        svn_wc_entry_t tmp_entry;

        SVN_ERR(svn_wc__get_entry(&dst_entry, db, dst_abspath, TRUE,
                                  svn_node_dir, TRUE,
                                  scratch_pool, scratch_pool));
        SVN_ERR(determine_copyfrom_info(&copyfrom_url, &copyfrom_rev, db,
                                        src_abspath,
                                        dst_entry ? dst_entry->url : NULL,
                                        dst_entry ? dst_entry->revision
                                                  : SVN_INVALID_REVNUM,
                                        scratch_pool, scratch_pool));

        /* The URL for a copied dir won't exist in the repository, which
           will cause  svn_wc_add4() below to fail.  Set the URL to the
           URL of the first copy for now to prevent this. */
        tmp_entry.url = apr_pstrdup(scratch_pool, copyfrom_url);
        SVN_ERR(svn_wc__entry_modify2(db, dst_abspath, svn_node_dir, FALSE,
                                      &tmp_entry, SVN_WC__ENTRY_MODIFY_URL,
                                      scratch_pool));
      }
    else
      {
        SVN_ERR(svn_wc__internal_get_ancestry(&copyfrom_url, &copyfrom_rev,
                                              db, src_abspath, scratch_pool,
                                              scratch_pool));
      }

    return svn_wc_add4(wc_ctx, dst_abspath, svn_depth_infinity,
                       copyfrom_url, copyfrom_rev,
                       cancel_func, cancel_baton,
                       notify_copied, notify_baton, scratch_pool);
  }
}



/* Public Interface */

svn_error_t *
svn_wc_copy3(svn_wc_context_t *wc_ctx,
             const char *src_abspath,
             const char *dst_abspath,
             svn_cancel_func_t cancel_func,
             void *cancel_baton,
             svn_wc_notify_func2_t notify_func,
             void *notify_baton,
             apr_pool_t *scratch_pool)
{
  svn_node_kind_t src_kind;
  const svn_wc_entry_t *dst_entry, *src_entry;
  svn_wc__db_kind_t kind;
  const char *dstdir_abspath, *dst_basename;

  svn_dirent_split(dst_abspath, &dstdir_abspath, &dst_basename, scratch_pool);

  SVN_ERR(svn_wc__get_entry_versioned(&dst_entry, wc_ctx, dstdir_abspath,
                                      svn_node_dir, FALSE, FALSE,
                                      scratch_pool, scratch_pool));
  SVN_ERR(svn_wc__get_entry_versioned(&src_entry, wc_ctx, src_abspath,
                                      svn_node_unknown, FALSE, FALSE,
                                      scratch_pool, scratch_pool));

  if ((src_entry->repos != NULL && dst_entry->repos != NULL) &&
      strcmp(src_entry->repos, dst_entry->repos) != 0)
    return svn_error_createf
      (SVN_ERR_WC_INVALID_SCHEDULE, NULL,
       _("Cannot copy to '%s', as it is not from repository '%s'; "
         "it is from '%s'"),
       svn_dirent_local_style(dst_abspath, scratch_pool),
       src_entry->repos, dst_entry->repos);
  if (dst_entry->schedule == svn_wc_schedule_delete)
    return svn_error_createf
      (SVN_ERR_WC_INVALID_SCHEDULE, NULL,
       _("Cannot copy to '%s' as it is scheduled for deletion"),
       svn_dirent_local_style(dst_abspath, scratch_pool));

  /* TODO(#2843): Rework the error report. */
  /* Check if the copy target is missing or hidden and thus not exist on the
     disk, before actually doing the file copy. */
  SVN_ERR(svn_wc__db_read_kind(&kind, wc_ctx->db, dst_abspath, TRUE,
                               scratch_pool));

  if (kind != svn_wc__db_kind_unknown)
    {
      svn_wc__db_status_t status;

      SVN_ERR(svn_wc__db_read_info(&status, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL,
                                   wc_ctx->db, dst_abspath,
                                   scratch_pool, scratch_pool));

      switch (status)
        {
          case svn_wc__db_status_excluded:
            return svn_error_createf(
                     SVN_ERR_ENTRY_EXISTS, NULL,
                     _("'%s' is already under version control "
                       "but is excluded."),
                     svn_dirent_local_style(dst_abspath, scratch_pool));
          case svn_wc__db_status_absent:
            return svn_error_createf(
                     SVN_ERR_ENTRY_EXISTS, NULL,
                     _("'%s' is already under version control"),
                     svn_dirent_local_style(dst_abspath, scratch_pool));

          /* Explicitly ignore other statii */
          default:
            break;
        }
    }

  SVN_ERR(svn_io_check_path(src_abspath, &src_kind, scratch_pool));

  if (src_kind == svn_node_file ||
      (src_entry->kind == svn_node_file && src_kind == svn_node_none))
    {
      /* Check if we are copying a file scheduled for addition,
         these require special handling. */
      if (src_entry->schedule == svn_wc_schedule_add
          && (! src_entry->copied))
        {
          SVN_ERR(copy_added_file_administratively(wc_ctx,
                                                   src_abspath, TRUE,
                                                   dst_abspath,
                                                   cancel_func, cancel_baton,
                                                   notify_func, notify_baton,
                                                   scratch_pool));
        }
      else
        {
          SVN_ERR(copy_file_administratively(wc_ctx,
                                             src_abspath,
                                             dst_abspath,
                                             cancel_func, cancel_baton,
                                             notify_func, notify_baton,
                                             scratch_pool));
        }
    }
  else if (src_kind == svn_node_dir)
    {
      /* Check if we are copying a directory scheduled for addition,
         these require special handling. */
      if (src_entry->schedule == svn_wc_schedule_add
          && (! src_entry->copied))
        {
          SVN_ERR(copy_added_dir_administratively(wc_ctx,
                                                  src_abspath, TRUE,
                                                  dst_abspath,
                                                  cancel_func, cancel_baton,
                                                  notify_func, notify_baton,
                                                  scratch_pool));
        }
      else
        {
          SVN_ERR(copy_dir_administratively(wc_ctx,
                                            src_abspath,
                                            dst_abspath,
                                            cancel_func, cancel_baton,
                                            notify_func, notify_baton,
                                            scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}
