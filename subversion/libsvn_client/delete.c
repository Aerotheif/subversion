/*
 * delete.c:  wrappers around wc delete functionality.
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

/* ==================================================================== */



/*** Includes. ***/

#include <apr_file_io.h>
#include "svn_types.h"
#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "client.h"



/*** Code. ***/

struct status_baton
{
  svn_error_t *err; /* the error generated for an undeletable path. */
};


/* An svn_wc_status_func_t callback function for finding
   status structures which are not safely deletable. */
static void
find_undeletables (void *baton,
                   const char *path,
                   svn_wc_status_t *status)
{
  struct status_baton *sb = baton;

  /* If we already have an error, don't lose that fact. */
  if (sb->err)
    return;

  /* Check for error-ful states. */
  if (status->text_status == svn_wc_status_obstructed)
    sb->err = svn_error_createf (SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                                 "'%s' is in the way of the resource "
                                 "actually under version control", path);
  else if (! status->entry)
    sb->err = svn_error_createf (SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                                 "'%s' is not under version control", path);

  else if ((status->text_status != svn_wc_status_normal
            && status->text_status != svn_wc_status_deleted
            && status->text_status != svn_wc_status_missing)
           ||
           (status->prop_status != svn_wc_status_none
            && status->prop_status != svn_wc_status_normal))
    sb->err = svn_error_createf (SVN_ERR_CLIENT_MODIFIED, NULL,
                             "'%s' has local modifications", path);
}


svn_error_t *
svn_client__can_delete (const char *path,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *pool)
{
  struct status_baton sb;
  svn_opt_revision_t revision;
  revision.kind = svn_opt_revision_unspecified;
  sb.err = SVN_NO_ERROR;
  SVN_ERR (svn_client_status (NULL, path, &revision, find_undeletables, &sb,
                              TRUE, FALSE, FALSE, FALSE, ctx, pool));
  return sb.err;
}


static svn_error_t *
path_driver_cb_func (void **dir_baton,
                     void *parent_baton,
                     void *callback_baton,
                     const char *path,
                     apr_pool_t *pool)
{
  const svn_delta_editor_t *editor = callback_baton;
  *dir_baton = NULL;
  return editor->delete_entry (path, SVN_INVALID_REVNUM, parent_baton, pool);
}


static svn_error_t *
delete_urls (svn_client_commit_info_t **commit_info,
             const apr_array_header_t *paths,
             svn_client_ctx_t *ctx,
             apr_pool_t *pool)
{
  void *ra_baton, *session;
  svn_ra_plugin_t *ra_lib;
  const svn_delta_editor_t *editor;
  void *edit_baton;
  void *commit_baton;
  const char *log_msg;
  svn_node_kind_t kind;
  apr_array_header_t *targets;
  svn_error_t *err;
  const char *common;
  int i;

  /* Condense our list of deletion targets. */
  SVN_ERR (svn_path_condense_targets (&common, &targets, paths, TRUE, pool));
  if (! targets->nelts)
    {
      const char *bname;
      svn_path_split (common, &common, &bname, pool);
      APR_ARRAY_PUSH (targets, const char *) = bname;
    }

  /* Create new commit items and add them to the array. */
  if (ctx->log_msg_func)
    {
      svn_client_commit_item_t *item;
      const char *tmp_file;
      apr_array_header_t *commit_items
        = apr_array_make (pool, targets->nelts, sizeof (item));

      for (i = 0; i < targets->nelts; i++)
        {
          const char *path = APR_ARRAY_IDX (targets, i, const char *);
          item = apr_pcalloc (pool, sizeof (*item));
          item->url = svn_path_join (common, path, pool);
          item->state_flags = SVN_CLIENT_COMMIT_ITEM_DELETE;
          APR_ARRAY_PUSH (commit_items, svn_client_commit_item_t *) = item;
        }
      SVN_ERR ((*ctx->log_msg_func) (&log_msg, &tmp_file, commit_items,
                                     ctx->log_msg_baton, pool));
      if (! log_msg)
        return SVN_NO_ERROR;
    }
  else
    log_msg = "";

  /* Get the RA vtable that matches URL. */
  SVN_ERR (svn_ra_init_ra_libs (&ra_baton, pool));
  SVN_ERR (svn_ra_get_ra_library (&ra_lib, ra_baton, common, pool));

  /* Open an RA session for the URL. Note that we don't have a local
     directory, nor a place to put temp files. */
  SVN_ERR (svn_client__open_ra_session (&session, ra_lib, common, NULL,
                                        NULL, NULL, FALSE, TRUE,
                                        ctx, pool));

  /* Verify that each thing to be deleted actually exists (to prevent
     the creation of a revision that has no changes, since the
     filesystem allows for no-op deletes). */
  for (i = 0; i < targets->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX (targets, i, const char *);
      path = svn_path_uri_decode (path, pool);
      APR_ARRAY_IDX (targets, i, const char *) = path;
      SVN_ERR (ra_lib->check_path (session, path, SVN_INVALID_REVNUM,
                                   &kind, pool));
      if (kind == svn_node_none)
        return svn_error_createf (SVN_ERR_FS_NOT_FOUND, NULL,
                                  "URL '%s' does not exist", path);
    }

  /* Fetch RA commit editor */
  SVN_ERR (svn_client__commit_get_baton (&commit_baton, commit_info, pool));
  SVN_ERR (ra_lib->get_commit_editor (session, &editor, &edit_baton,
                                      log_msg, svn_client__commit_callback,
                                      commit_baton, pool));

  /* Call the path-based editor driver. */
  err = svn_delta_path_driver (editor, edit_baton, SVN_INVALID_REVNUM,
                               targets, path_driver_cb_func,
                               (void *)editor, pool);
  if (err)
    {
      /* At least try to abort the edit (and fs txn) before throwing err. */
      svn_error_clear (editor->abort_edit (edit_baton, pool));
      return err;
    }

  /* Close the edit. */
  SVN_ERR (editor->close_edit (edit_baton, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__wc_delete (const char *path,
                       svn_wc_adm_access_t *adm_access,
                       svn_boolean_t force,
                       svn_boolean_t dry_run,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *pool)
{

  if (!force)
    /* Verify that there are no "awkward" files */
    SVN_ERR (svn_client__can_delete (path, ctx, pool));

  if (!dry_run)
    /* Mark the entry for commit deletion and perform wc deletion */
    SVN_ERR (svn_wc_delete (path, adm_access,
                            ctx->cancel_func, ctx->cancel_baton,
                            ctx->notify_func, ctx->notify_baton, pool));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_delete (svn_client_commit_info_t **commit_info,
                   const apr_array_header_t *paths,
                   svn_boolean_t force,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  if (! paths->nelts)
    return SVN_NO_ERROR;

  if (svn_path_is_url (APR_ARRAY_IDX (paths, 0, const char *)))
    {
      SVN_ERR (delete_urls (commit_info, paths, ctx, pool));
    }
  else
    {
      apr_pool_t *subpool = svn_pool_create (pool);
      int i;

      for (i = 0; i < paths->nelts; i++)
        {
          svn_wc_adm_access_t *adm_access;
          const char *path = APR_ARRAY_IDX (paths, i, const char *);
          const char *parent_path;

          svn_pool_clear (subpool);
          parent_path = svn_path_dirname (path, subpool);

          /* See if the user wants us to stop. */
          if (ctx->cancel_func)
            SVN_ERR (ctx->cancel_func (ctx->cancel_baton));

          /* Let the working copy library handle the PATH. */
          SVN_ERR (svn_wc_adm_open2 (&adm_access, NULL, parent_path,
                                     TRUE, 0, subpool));
          SVN_ERR (svn_client__wc_delete (path, adm_access, force,
                                          FALSE, ctx, subpool));
          SVN_ERR (svn_wc_adm_close (adm_access));
        }
      svn_pool_destroy (subpool);
    }

  return SVN_NO_ERROR;
}
