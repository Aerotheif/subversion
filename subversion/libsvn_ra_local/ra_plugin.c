/*
 * ra_plugin.c : the main RA module for local repository access
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include "ra_local.h"



/*----------------------------------------------------------------*/

/** The commit cleanup routine passed as a "hook" to the filesystem
    editor **/

/* (This is a routine of type svn_fs_commit_hook_t) */
static svn_error_t *
cleanup_commit (svn_revnum_t new_revision, void *baton)
{
  /* Recover our hook baton: */
  /*  svn_ra_local__commit_closer_t *closer =
      (svn_ra_local__commit_closer_t *) baton; */

  /* Call closer->close_func() on each committed target! */
  /* TODO */

  return SVN_NO_ERROR;
}



/*----------------------------------------------------------------*/

/** The reporter routines (for updates) **/


static svn_error_t *
set_directory (void *report_baton,
               svn_string_t *dir_path,
               svn_revnum_t revision)
{
  /* TODO:  someday when we try to get updates working */

  return SVN_NO_ERROR;
}


static svn_error_t *
set_file (void *report_baton,
          svn_string_t *file_path,
          svn_revnum_t revision)
{
  /* TODO:  someday when we try to get updates working */

  return SVN_NO_ERROR;
}



static svn_error_t *
finish_report (void *report_baton)
{
  /* TODO:  someday when we try to get updates working */

  return SVN_NO_ERROR;
}




/*----------------------------------------------------------------*/

/** The RA plugin routines **/


static svn_error_t *
open (void **session_baton,
      svn_string_t *repository_URL,
      apr_pool_t *pool)
{
  svn_ra_local__session_baton_t *baton;

  /* When we close the session_baton later, we don't necessarily want
     to kill the main caller's pool; so let's subpool and work from
     there. */
  apr_pool_t *subpool = svn_pool_create (pool);

  /* Allocate the session_baton the parent pool */
  baton = apr_pcalloc (pool, sizeof(*baton));

  /* And let all other session_baton data use session's subpool */
  baton->pool = subpool;
  baton->repository_URL = repository_URL;
  baton->fs = svn_fs_new (subpool);

  /* Look through the URL, figure out which part points to the
     repository, and which part is the path *within* the
     repository. */
  SVN_ERR (svn_ra_local__split_URL (&(baton->repos_path),
                                    &(baton->fs_path),
                                    baton->repository_URL,
                                    subpool));

  /* Temporary... for debugging only.  Obviously, this library should
     never print to stdout! */
  printf ("Repos: %s, Path: %s\n",
          baton->repos_path->data,
          baton->fs_path->data);

  /* Open the filesystem at located at environment `repos_path' */
  SVN_ERR (svn_fs_open_berkeley (baton->fs, baton->repos_path->data));

  /* Return the session baton */
  *session_baton = baton;
  return SVN_NO_ERROR;
}



static svn_error_t *
close (void *session_baton)
{
  svn_ra_local__session_baton_t *baton =
    (svn_ra_local__session_baton_t *) session_baton;

  /* Close the repository filesystem */
  SVN_ERR (svn_fs_close_fs (baton->fs));

  /* Free all memory allocated during this ra session.  */
  apr_pool_destroy (baton->pool);

  return SVN_NO_ERROR;
}




static svn_error_t *
get_latest_revnum (void *session_baton,
                   svn_revnum_t *latest_revnum)
{
  svn_ra_local__session_baton_t *baton =
    (svn_ra_local__session_baton_t *) session_baton;

  SVN_ERR (svn_fs_youngest_rev (latest_revnum, baton->fs, baton->pool));

  return SVN_NO_ERROR;
}




static svn_error_t *
get_commit_editor (void *session_baton,
                   const svn_delta_edit_fns_t **editor,
                   void **edit_baton,
                   svn_revnum_t base_revision,
                   svn_string_t *base_path,
                   svn_string_t *log_msg,
                   svn_ra_close_commit_func_t close_func,
                   svn_ra_set_wc_prop_func_t set_func,
                   void *close_baton)
{
  svn_delta_edit_fns_t *commit_editor, *tracking_editor;
  const svn_delta_edit_fns_t *composed_editor;
  void *commit_editor_baton, *tracking_editor_baton, *composed_editor_baton;

  svn_ra_local__session_baton_t *sess_baton =
    (svn_ra_local__session_baton_t *) session_baton;

  /* Construct a Magick commit-hook baton */
  svn_ra_local__commit_closer_t *closer
    = apr_pcalloc (sess_baton->pool, sizeof(*closer));

  closer->pool = sess_baton->pool;
  closer->close_func = close_func;
  closer->set_func = set_func;
  closer->close_baton = close_baton;
  closer->target_array = apr_pcalloc (closer->pool,
                                          sizeof(*(closer->target_array)));

  /* Get the filesystem commit-editor */
  SVN_ERR (svn_fs_get_editor (&commit_editor, &commit_editor_baton,
                              sess_baton->fs,
                              base_revision, base_path,
                              log_msg,
                              cleanup_commit, closer,
                              sess_baton->pool));

  /* Get the commit `tracking' editor, telling it to store committed
     targets inside our `closer' object. */
  SVN_ERR (svn_ra_local__get_commit_track_editor (&tracking_editor,
                                                  &tracking_editor_baton,
                                                  sess_baton->pool,
                                                  closer));

  /* Set up a pipeline between the editors, creating a wrapper editor. */
  svn_delta_compose_editors (&composed_editor, &composed_editor_baton,
                             commit_editor, commit_editor_baton,
                             tracking_editor, tracking_editor_baton,
                             sess_baton->pool);

  /* Give the magic composed-editor back to the client */
  *editor = composed_editor;
  *edit_baton = composed_editor_baton;
  return SVN_NO_ERROR;
}



static svn_error_t *
do_checkout (void *session_baton,
             const svn_delta_edit_fns_t *editor,
             void *edit_baton)
{
  /* TODO:  someday */

  return SVN_NO_ERROR;
}



static svn_error_t *
do_update (void *session_baton,
           const svn_ra_reporter_t **reporter,
           void **report_baton,
           apr_array_header_t *targets,
           const svn_delta_edit_fns_t *update_editor,
           void *update_baton)
{
  /* TODO:  someday */

  return SVN_NO_ERROR;
}



/*----------------------------------------------------------------*/

/** The static reporter and ra_plugin objects **/

static const svn_ra_reporter_t ra_local_reporter =
{
  set_directory,
  set_file,
  finish_report
};


static const svn_ra_plugin_t ra_local_plugin =
{
  "ra_local",
  "RA module for accessing repository on local disk. (file:// URLs)",
  open,
  close,
  get_latest_revnum,
  get_commit_editor,
  do_checkout,
  do_update
};


/*----------------------------------------------------------------*/

/** The One Public Routine, called by libsvn_client **/

svn_error_t *
svn_ra_local_init (int abi_version,
                   apr_pool_t *pool,
                   const char **url_type,
                   const svn_ra_plugin_t **plugin)
{
  *url_type = "file";
  *plugin = &ra_local_plugin;

  /* ben sez:  todo:  check that abi_version >=1. */

  return SVN_NO_ERROR;
}








/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
