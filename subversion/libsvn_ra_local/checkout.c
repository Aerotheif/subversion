/*
 * checkout.c : read a repository tree and drive a checkout editor.
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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

#include "ra_local.h"
#include <assert.h>
#include "svn_pools.h"


/* Helper to read data out of a file at ROOT:PATH and push it to
   EDITOR via FILE_BATON.

   ben sez: whoa.  The elegance and level of abstraction going on here
   is amazing.  What an amazing design.  It's like a set of opaque
   legos that all perfectly fit together. :) */
static svn_error_t *
send_file_contents (svn_fs_root_t *root,
                    svn_stringbuf_t *path,
                    void *file_baton,
                    const svn_delta_editor_t *editor,
                    apr_pool_t *pool)
{
  svn_stream_t *contents;
  svn_txdelta_window_handler_t handler;
  void *handler_baton;

  /* Get a readable stream of the file's contents. */
  SVN_ERR (svn_fs_file_contents (&contents, root, path->data, pool));

  /* Get an editor func that wants to consume the delta stream. */
  SVN_ERR (editor->apply_textdelta (file_baton, &handler, &handler_baton));

  /* Send the file's contents to the delta-window handler. */
  SVN_ERR (svn_txdelta_send_stream (contents, handler, handler_baton, pool));

  return SVN_NO_ERROR;
}



/* Helper to push any properties attached to ROOT:PATH at EDITOR,
   using OBJECT_BATON.  IS_DIR indicates which editor func to call. */
static svn_error_t *
set_any_props (svn_fs_root_t *root,
               const svn_string_t *path,
               void *object_baton,
               const svn_delta_editor_t *editor,
               int is_dir,
               apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  svn_revnum_t committed_rev;
  svn_string_t *last_author, *committed_date;
  char *revision_str = NULL;
  apr_hash_t *props = NULL;

  /* Get all user properties attached to PATH. */
  SVN_ERR (svn_fs_node_proplist (&props, root, path->data, pool));

  /* Query the fs for three 'entry' props:  specifically, the
     last-changed-rev of the file or dir ("created rev"), and the
     associated date & author of said revision.  Add these three props
     to the proplist hash, as a means of getting them into the working
     copy's 'entries' file.  The working copy Update Editor will
     recognize them. */
  if ((props == NULL) || (apr_hash_count (props) == 0))
    props = apr_hash_make (pool);

  SVN_ERR (svn_repos_get_committed_info (&committed_rev,
                                         &committed_date,
                                         &last_author,
                                         root, path, pool));

  revision_str = apr_psprintf (pool, "%" SVN_REVNUM_T_FMT, committed_rev);
  apr_hash_set (props, SVN_PROP_ENTRY_COMMITTED_REV,
                strlen(SVN_PROP_ENTRY_COMMITTED_REV),
                svn_stringbuf_create (revision_str, pool));

  apr_hash_set (props, SVN_PROP_ENTRY_COMMITTED_DATE,
                strlen(SVN_PROP_ENTRY_COMMITTED_DATE), committed_date);

  apr_hash_set (props, SVN_PROP_ENTRY_LAST_AUTHOR,
                strlen(SVN_PROP_ENTRY_LAST_AUTHOR), last_author);


  /* Loop over properties, send them through the editor. */
  for (hi = apr_hash_first (pool, props); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      apr_ssize_t klen;
      const char *name;
      svn_string_t *value;

      apr_hash_this (hi, &key, &klen, &val);
      name = apr_pstrndup (pool, key, klen);
      value = val;

      if (is_dir)
        SVN_ERR (editor->change_dir_prop (object_baton, name, value, pool));
      else
        SVN_ERR (editor->change_file_prop (object_baton, name, value, pool));
    }

  return SVN_NO_ERROR;
}



/* A depth-first recursive walk of DIR_PATH under a fs ROOT that adds
   dirs and files via EDITOR and DIR_BATON.  URL represents the
   current repos location, and is stored in DIR_BATON's working copy.
   EDIT_PATH keeps track of this directory's path relative to the root
   of the edit.

   Note: we're conspicuously creating a subpool in POOL and freeing it
   at each level of subdir recursion; this is a safety measure that
   protects us when checking out outrageously large or deep trees.
   Also, we have a per-iteration subpool which is clear after being
   used for each directory entry.

   Note: we aren't driving EDITOR with "postfix" text deltas; that
   style only exists to recognize skeletal conflicts as early as
   possible (during a commit).  There are no conflicts in a checkout,
   however.  :) */
static svn_error_t *
walk_tree (svn_fs_root_t *root,
           const svn_string_t *dir_path,
           svn_stringbuf_t *edit_path,
           void *dir_baton,
           const svn_delta_editor_t *editor,
           void *edit_baton,
           svn_stringbuf_t *URL,
           svn_boolean_t recurse,
           apr_pool_t *pool)
{
  apr_hash_t *dirents;
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create (pool);
  svn_stringbuf_t *URL_path = svn_stringbuf_dup (URL, pool);
  svn_stringbuf_t *dirent_path =
    svn_stringbuf_create_from_string (dir_path, pool);

  if (! edit_path)
    edit_path = svn_stringbuf_create ("", pool);

  SVN_ERR (svn_fs_dir_entries (&dirents, root, dir_path->data, pool));

  /* Loop over this directory's dirents: */
  for (hi = apr_hash_first (pool, dirents); hi; hi = apr_hash_next (hi))
    {
      int is_dir, is_file;
      const void *key;
      void *val;
      apr_ssize_t klen;
      svn_fs_dirent_t *dirent;
      svn_stringbuf_t *dirent_name;
      svn_string_t dirent_str;

      apr_hash_this (hi, &key, &klen, &val);
      dirent = (svn_fs_dirent_t *) val;
      dirent_name = svn_stringbuf_create (dirent->name, subpool);

      /* Extend our various paths by DIRENT_NAME. */
      svn_path_add_component (dirent_path, dirent_name);
      svn_path_add_component (URL_path, dirent_name);
      svn_path_add_component (edit_path, dirent_name);

      /* What is dirent? */
      SVN_ERR (svn_fs_is_dir (&is_dir, root, dirent_path->data, subpool));
      SVN_ERR (svn_fs_is_file (&is_file, root, dirent_path->data, subpool));

      dirent_str.data = dirent_path->data;
      dirent_str.len = dirent_path->len;

      if (is_dir && recurse)
        {
          void *new_dir_baton;

          /* We pass 2 invalid ancestry args, which allows the editor
             to infer them via inheritance.  We do *not* pass real
             args, since we're not referencing any existing working
             copy paths.  We don't want the editor to "copy" anything. */
          SVN_ERR (editor->add_directory (edit_path->data, dir_baton,
                                          NULL, SVN_INVALID_REVNUM,
                                          subpool, &new_dir_baton));
          SVN_ERR (set_any_props (root, &dirent_str, new_dir_baton,
                                  editor, 1, subpool));
          /* Recurse */
          SVN_ERR (walk_tree (root, &dirent_str, edit_path,
                              new_dir_baton, editor, edit_baton,
                              URL_path, recurse, subpool));
        }

      else if (is_file)
        {
          void *file_baton;

          SVN_ERR (editor->add_file (edit_path->data, dir_baton,
                                     URL_path->data, SVN_INVALID_REVNUM,
                                     subpool, &file_baton));
          SVN_ERR (set_any_props (root, &dirent_str, file_baton,
                                  editor, 0, subpool));
          SVN_ERR (send_file_contents (root, dirent_path, file_baton,
                                       editor, subpool));
          SVN_ERR (editor->close_file (file_baton));
        }

      else
        {
          /* It's not a file or dir.  What the heck?  Instead of
             returning an error, let's just ignore the thing. */
        }

      /* Restore EDIT_PATH. URL_PATH, and DIRENT_PATH to their
         original selves. */
      svn_stringbuf_chop (edit_path, dirent_name->len + 1);
      svn_stringbuf_chop (URL_path, dirent_name->len + 1);
      svn_stringbuf_chop (dirent_path, dirent_name->len + 1);

      /* Clear out our per-iteration pool. */
      svn_pool_clear (subpool);
    }

  /* Close the dir and remove the subpool we used at this level. */
  SVN_ERR (editor->close_directory (dir_baton));

  /* Destory our subpool. */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}



/* The main editor driver.  Short and elegant! */
svn_error_t *
svn_ra_local__checkout (svn_fs_t *fs,
                        svn_revnum_t revnum,
                        svn_boolean_t recurse,
                        svn_stringbuf_t *URL,
                        const svn_string_t *fs_path,
                        const svn_delta_editor_t *editor,
                        void *edit_baton,
                        apr_pool_t *pool)
{
  svn_fs_root_t *root;
  void *baton;

  /* Get the revision that is being checked out. */
  SVN_ERR (svn_fs_revision_root (&root, fs, revnum, pool));

  /* Call some initial editor functions. */
  SVN_ERR (editor->set_target_revision (edit_baton, revnum));
  SVN_ERR (editor->open_root (edit_baton, SVN_INVALID_REVNUM, pool, &baton));
  SVN_ERR (set_any_props (root, fs_path, baton, editor, 1, pool));

  /* Walk the tree. */
  SVN_ERR (walk_tree (root, fs_path, NULL, baton, editor, edit_baton,
                      URL, recurse, pool));

  /* Finalize the edit drive. */
  SVN_ERR (editor->close_edit (edit_baton));

  return SVN_NO_ERROR;
}


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end: */
