/* fs-helpers.c --- tests for the filesystem
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */

#include <stdlib.h>
#include <string.h>
#include <apr_pools.h>
#include "svn_error.h"
#include "svn_fs.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_test.h"
#include "fs-helpers.h"

#include "../libsvn_fs/fs.h"
#include "../libsvn_fs/dag.h"
#include "../libsvn_fs/rev-table.h"
#include "../libsvn_fs/nodes-table.h"
#include "../libsvn_fs/trail.h"


/*-------------------------------------------------------------------*/

/** Helper routines. **/


/* Generic Berkeley DB error handler function. */
static void
berkeley_error_handler (const char *errpfx,
                                    char *msg)
{
  fprintf (stderr, "%s%s\n", errpfx ? errpfx : "", msg);
}


svn_error_t *
svn_test__fs_new (svn_fs_t **fs_p, apr_pool_t *pool)
{
  *fs_p = svn_fs_new (pool);
  if (! *fs_p)
    return svn_error_create (SVN_ERR_FS_GENERAL, 0, NULL, pool,
                             "Couldn't alloc a new fs object.");

  /* Provide a warning function that just dumps the message to stderr.  */
  svn_fs_set_warning_func (*fs_p, svn_handle_warning, 0);

  return SVN_NO_ERROR;
}



svn_error_t *
svn_test__create_fs_and_repos (svn_fs_t **fs_p,
                               const char *name,
                               apr_pool_t *pool)
{
  apr_finfo_t finfo;

  /* If there's already a repository named NAME, delete it.  Doing
     things this way means that repositories stick around after a
     failure for postmortem analysis, but also that tests can be
     re-run without cleaning out the repositories created by prior
     runs.  */
  if (apr_stat (&finfo, name, APR_FINFO_TYPE, pool) == APR_SUCCESS)
    {
      if (finfo.filetype == APR_DIR)
        SVN_ERR (svn_fs_delete_berkeley (name, pool));
      else
        return svn_error_createf (SVN_ERR_TEST_FAILED, 0, NULL, pool,
                                  "there is already a file named `%s'", name);
    }

  SVN_ERR (svn_test__fs_new (fs_p, pool));
  SVN_ERR (svn_fs_create_berkeley (*fs_p, name));

  /* Provide a handler for Berkeley DB error messages.  */
  SVN_ERR (svn_fs_set_berkeley_errcall (*fs_p, berkeley_error_handler));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_test__stream_to_string (svn_string_t **string,
                            svn_stream_t *stream,
                            apr_pool_t *pool)
{
  char buf[50];
  apr_size_t len;
  svn_string_t *str = svn_string_create ("", pool);

  do
    {
      /* "please read 40 bytes into buf" */
      len = 40;
      SVN_ERR (svn_stream_read (stream, buf, &len));

      /* Now copy however many bytes were *actually* read into str. */
      svn_string_appendbytes (str, buf, len);

    } while (len);  /* Continue until we're told that no bytes were
                       read. */

  *string = str;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_test__set_file_contents (svn_fs_root_t *root,
                             const char *path,
                             const char *contents,
                             apr_pool_t *pool)
{
  svn_txdelta_window_handler_t consumer_func;
  void *consumer_baton;
  svn_string_t *wstring = svn_string_create (contents, pool);

  SVN_ERR (svn_fs_apply_textdelta (&consumer_func, &consumer_baton,
                                   root, path, pool));
  SVN_ERR (svn_txdelta_send_string (wstring, consumer_func,
                                    consumer_baton, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_test__get_file_contents (svn_fs_root_t *root,
                             const char *path,
                             svn_string_t **str,
                             apr_pool_t *pool)
{
  svn_stream_t *stream;

  SVN_ERR (svn_fs_file_contents (&stream, root, path, pool));
  SVN_ERR (svn_test__stream_to_string (str, stream, pool));

  return SVN_NO_ERROR;
}


/* Read all the entries in directory PATH under transaction or
   revision root ROOT, copying their full paths into the TREE_ENTRIES
   hash, and recursing when those entries are directories */
static svn_error_t *
get_dir_entries (apr_hash_t *tree_entries,
                 svn_fs_root_t *root,
                 svn_string_t *path,
                 apr_pool_t *pool)
{
  apr_hash_t *entries;
  apr_hash_index_t *hi;

  SVN_ERR (svn_fs_dir_entries (&entries, root, path->data, pool));

  /* Copy this list to the master list with the path prepended to the
     names */
  for (hi = apr_hash_first (entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_size_t keylen;
      void *val;
      svn_fs_dirent_t *dirent;
      svn_string_t *full_path;
      int is_dir;

      apr_hash_this (hi, &key, &keylen, &val);
      dirent = val;

      /* Calculate the full path of this entry (by appending the name
         to the path thus far) */
      full_path = svn_string_dup (path, pool);
      svn_path_add_component (full_path,
                              svn_string_create (dirent->name, pool),
                              svn_path_repos_style);

      /* Now, copy this dirent to the master hash, but this time, use
         the full path for the key */
      apr_hash_set (tree_entries, full_path->data,
                    APR_HASH_KEY_STRING, dirent);

      /* If this entry is a directory, recurse into the tree. */
      SVN_ERR (svn_fs_is_dir (&is_dir, root, full_path->data, pool));
      if (is_dir)
        SVN_ERR (get_dir_entries (tree_entries, root, full_path, pool));
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
validate_tree_entry (svn_fs_root_t *root,
                     svn_test__tree_entry_t *entry,
                     apr_pool_t *pool)
{
  svn_stream_t *rstream;
  svn_string_t *rstring;
  int is_dir;

  /* Verify that this is the expected type of node */
  SVN_ERR (svn_fs_is_dir (&is_dir, root, entry->path, pool));
  if ((!is_dir && !entry->contents) || (is_dir && entry->contents))
    return svn_error_createf
      (SVN_ERR_FS_GENERAL, 0, NULL, pool,
       "node `%s' in tree was of unexpected node type",
       entry->path);

  /* Verify that the contents are as expected (files only) */
  if (! is_dir)
    {
      SVN_ERR (svn_fs_file_contents (&rstream, root, entry->path, pool));
      SVN_ERR (svn_test__stream_to_string (&rstring, rstream, pool));
      if (! svn_string_compare (rstring,
                                svn_string_create (entry->contents, pool)))
        return svn_error_createf
          (SVN_ERR_FS_GENERAL, 0, NULL, pool,
           "node `%s' in tree had unexpected contents",
           entry->path);
    }

  return SVN_NO_ERROR;
}



/* Given a transaction or revision root (ROOT), check to see if the
   tree that grows from that root has all the path entries, and only
   those entries, passed in the array ENTRIES (which is an array of
   NUM_ENTRIES tree_test_entry_t's) */
svn_error_t *
svn_test__validate_tree (svn_fs_root_t *root,
                         svn_test__tree_entry_t *entries,
                         int num_entries,
                         apr_pool_t *pool)
{
  apr_hash_t *tree_entries;
  int i;
  apr_pool_t *subpool = svn_pool_create (pool);
  svn_string_t *root_dir = svn_string_create ("", subpool);

  /* Create our master hash for storing the entries */
  tree_entries = apr_hash_make (pool);

  /* Begin the recursive directory entry dig */
  SVN_ERR (get_dir_entries (tree_entries, root, root_dir, subpool));

  if (num_entries < apr_hash_count (tree_entries))
    return svn_error_create
      (SVN_ERR_FS_GENERAL, 0, NULL, pool,
       "unexpected number of items in tree (too many)");
  if (num_entries > apr_hash_count (tree_entries))
    return svn_error_create
      (SVN_ERR_FS_GENERAL, 0, NULL, pool,
       "unexpected number of items in tree (too few)");

  if ((num_entries > 0) && (! entries))
    return svn_error_create
      (SVN_ERR_TEST_FAILED, 0, NULL, pool,
       "validation requested against non-existant control data");

  for (i = 0; i < num_entries; i++)
    {
      void *val;

      /* Verify that the entry exists in our full list of entries. */
      val = apr_hash_get (tree_entries, entries[i].path, APR_HASH_KEY_STRING);
      if (! val)
        return svn_error_createf
          (SVN_ERR_FS_GENERAL, 0, NULL, pool,
           "failed to find expected node `%s' in tree",
           entries[i].path);
      SVN_ERR (validate_tree_entry (root, &entries[i], subpool));
    }

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_test__txn_script_exec (svn_fs_root_t *txn_root,
                           svn_test__txn_script_command_t *script,
                           int num_edits,
                           apr_pool_t *pool)
{
  int i;

  /* Run through the list of edits, making the appropriate edit on
     that entry in the TXN_ROOT. */
  for (i = 0; i < num_edits; i++)
    {
      const char *path = script[i].path;
      const char *contents = script[i].contents;
      int cmd = script[i].cmd;
      int is_dir = (contents == 0);

      switch (cmd)
        {
        case '+':
          if (is_dir)
            {
              SVN_ERR (svn_fs_make_dir (txn_root, path, pool));
            }
          else
            {
              SVN_ERR (svn_fs_make_file (txn_root, path, pool));
              SVN_ERR (svn_test__set_file_contents (txn_root, path,
                                                    contents, pool));
            }
          break;

        case '-':
          SVN_ERR (svn_fs_delete_tree (txn_root, path, pool));
          break;

        case '>':
          if (! is_dir)
            {
              SVN_ERR (svn_test__set_file_contents (txn_root, path,
                                                    contents, pool));
            }
          break;

        default:
          break;
        }
    }

  return SVN_NO_ERROR;
}





