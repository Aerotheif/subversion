/*
 * delta.c:   an editor driver for svn_repos_dir_delta
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


#include "svn_types.h"
#include "svn_delta.h"
#include "svn_fs.h"
#include "svn_path.h"
#include "apr_hash.h"
#include "svn_repos.h"



/* THINGS TODO:  Currently the code herein gives only a slight nod to
   fully supporting directory deltas that involve renames, copies, and
   such.  */


/* Some datatypes and declarations used throughout the file.  */


/* Parameters which remain constant throughout a delta traversal.
   At the top of the recursion, we initialize one of these structures.
   Then, we pass it down, unchanged, to every call.  This way,
   functions invoked deep in the recursion can get access to this
   traversal's global parameters, without using global variables.  */
struct context {
  const svn_delta_edit_fns_t *editor;
  svn_fs_root_t *source_root;
  apr_hash_t *source_rev_diffs;
  svn_fs_root_t *target_root;
};


/* The type of a function that accepts changes to an object's property
   list.  OBJECT is the object whose properties are being changed.
   NAME is the name of the property to change.  VALUE is the new value
   for the property, or zero if the property should be deleted.  */
typedef svn_error_t *proplist_change_fn_t (struct context *c,
                                           void *object,
                                           svn_stringbuf_t *name,
                                           svn_stringbuf_t *value,
                                           apr_pool_t *pool);



/* Some prototypes for functions used throughout.  See each individual
   function for information about what it does.  */


/* Retrieving the base revision from the path/revision hash.  */
static svn_revnum_t get_revision_from_hash (apr_hash_t *hash,
                                            svn_stringbuf_t *path,
                                            apr_pool_t *pool);


/* proplist_change_fn_t property changing functions.  */
static svn_error_t *change_dir_prop (struct context *c,
                                     void *object,
                                     svn_stringbuf_t *name,
                                     svn_stringbuf_t *value,
                                     apr_pool_t *pool);

static svn_error_t *change_file_prop (struct context *c,
                                      void *object,
                                      svn_stringbuf_t *name,
                                      svn_stringbuf_t *value,
                                      apr_pool_t *pool);


/* Constructing deltas for properties of files and directories.  */
static svn_error_t *delta_proplists (struct context *c,
                                     svn_stringbuf_t *source_path,
                                     svn_stringbuf_t *target_path,
                                     proplist_change_fn_t *change_fn,
                                     void *object,
                                     apr_pool_t *pool);


/* Constructing deltas for file constents.  */
static svn_error_t *send_text_delta (struct context *c,
                                     void *file_baton,
                                     svn_txdelta_stream_t *delta_stream,
                                     apr_pool_t *pool);

static svn_error_t *delta_files (struct context *c,
                                 void *file_baton,
                                 svn_stringbuf_t *source_path,
                                 svn_stringbuf_t *target_path,
                                 apr_pool_t *pool);


/* Generic directory deltafication routines.  */
static svn_error_t *delete (struct context *c,
                            void *dir_baton,
                            svn_stringbuf_t *target_entry,
                            apr_pool_t *pool);

static svn_error_t *add_file_or_dir (struct context *c,
                                     void *dir_baton,
                                     svn_stringbuf_t *target_parent,
                                     svn_stringbuf_t *target_entry,
                                     svn_stringbuf_t *source_parent,
                                     svn_stringbuf_t *source_entry,
                                     apr_pool_t *pool);

static svn_error_t *replace_file_or_dir (struct context *c,
                                         void *dir_baton,
                                         svn_stringbuf_t *target_parent,
                                         svn_stringbuf_t *target_entry,
                                         svn_stringbuf_t *source_parent,
                                         svn_stringbuf_t *source_entry,
                                         apr_pool_t *pool);

#if 0 /* comment out until used, to avoid compiler warning */
static svn_error_t *find_nearest_entry (svn_fs_dirent_t **s_entry,
                                        int *distance,
                                        struct context *c,
                                        svn_stringbuf_t *source_parent,
                                        svn_stringbuf_t *target_parent,
                                        svn_fs_dirent_t *t_entry,
                                        apr_pool_t *pool);
#endif /* 0 */

static svn_error_t *delta_dirs (struct context *c,
                                void *dir_baton,
                                svn_stringbuf_t *source_path,
                                svn_stringbuf_t *target_path,
                                apr_pool_t *pool);



/* Public interface to computing directory deltas.  */
svn_error_t *
svn_repos_update (svn_fs_root_t *target_root,
                  svn_fs_root_t *source_root,
                  svn_stringbuf_t *parent_dir,
                  svn_stringbuf_t *entry,
                  apr_hash_t *source_rev_diffs,
                  const svn_delta_edit_fns_t *editor,
                  void *edit_baton,
                  apr_pool_t *pool)
{
  void *root_baton;
  struct context c;
  int source_parent_is_dir;
  int target_parent_is_dir;
  svn_stringbuf_t *full_path;
  svn_fs_id_t *source_id;
  svn_fs_id_t *target_id;
  int distance;

  /* Source parent must be valid. */
  if (! parent_dir)
    {
      return
        svn_error_create
        (SVN_ERR_FS_PATH_SYNTAX, 0, 0, pool,
         "directory delta source parent path is invalid");
    }

  /* Target root must be a revision. */
  if (! svn_fs_is_revision_root (target_root))
    {
      return
        svn_error_create
        (SVN_ERR_FS_NOT_REVISION_ROOT, 0, 0, pool,
         "directory delta target not a revision root");
    }

  /* Check the node types of the parents -- they had better both be
     directories!  <voiceover fx="booming echo"> First thousand kids
     get an existance check FREE!! </voiceover>  Obviously, if the
     parent path is empty, we're looking at the root of the
     repository, which is guaranteed to be a directory.  */
  if (! svn_path_is_empty (parent_dir, svn_path_repos_style))
    {
      SVN_ERR (svn_fs_is_dir (&source_parent_is_dir, source_root,
                              parent_dir->data, pool));
      SVN_ERR (svn_fs_is_dir (&target_parent_is_dir, target_root,
                              parent_dir->data, pool));
      if (! source_parent_is_dir)
        {
          return
            svn_error_create
            (SVN_ERR_FS_NOT_DIRECTORY, 0, 0, pool,
             "directory delta source parent not a directory");
        }
      if (! target_parent_is_dir)
        {
          return
            svn_error_create
            (SVN_ERR_FS_NOT_DIRECTORY, 0, 0, pool,
             "directory delta target parent not a directory");
        }
    }

  /* Setup our pseudo-global structure here.  We need these variables
     throughout the deltafication process, so pass them around by
     reference to all the helper functions. */
  c.editor = editor;
  c.source_root = source_root;
  c.source_rev_diffs = source_rev_diffs;
  c.target_root = target_root;

  /* Set the global target revision. */
  SVN_ERR (editor->set_target_revision
           (edit_baton,
            svn_fs_revision_root_revision (target_root)));

  /* Call replace_root to get our root_baton... */
  SVN_ERR (editor->replace_root
           (edit_baton,
            get_revision_from_hash (source_rev_diffs,
                                    parent_dir,
                                    pool),
            &root_baton));

  /* Construct the full path of the update item. */
  full_path = svn_string_dup (parent_dir, pool);
  if (entry && entry->len > 0)
    svn_path_add_component (full_path, entry,
                            svn_path_repos_style);

  /* Get the node ids for the source and target paths. */
  SVN_ERR (svn_fs_node_id (&source_id, source_root,
                           full_path->data, pool));
  SVN_ERR (svn_fs_node_id (&target_id, target_root,
                           full_path->data, pool));

  if (entry && entry->len > 0)
    {
      /* Use the distance between the node ids to determine the best
         way to update the requested entry. */
      distance = svn_fs_id_distance (source_id, target_id);
      if (distance == 0)
        {
          /* They're the same node!  No-op (you gotta love those). */
        }
      else if (distance == -1)
        {
          /* The nodes are not related at all.  Delete the one, and
             add the other. */
          SVN_ERR (delete (&c, root_baton, entry, pool));
          SVN_ERR (add_file_or_dir
                   (&c, root_baton, parent_dir, entry,
                    0, 0, pool));
        }
      else
        {
          /* The nodes are at least related.  Just replace the one
             with the other. */
          SVN_ERR (replace_file_or_dir (&c, root_baton,
                                        parent_dir,
                                        entry,
                                        parent_dir,
                                        entry,
                                        pool));
        }
    }
  else
    {
      /* There is no entry given, so update the whole parent
         directory. */
      SVN_ERR (delta_dirs (&c, root_baton, full_path, full_path, pool));
    }

  /* Make sure we close the root directory we opened above. */
  SVN_ERR (editor->close_directory (root_baton));

  /* Close the edit. */
  SVN_ERR (editor->close_edit (edit_baton));

  /* All's well that ends well. */
  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_dir_delta (svn_fs_root_t *source_root,
                     svn_stringbuf_t *source_path,
                     apr_hash_t *source_rev_diffs,
                     svn_fs_root_t *target_root,
                     svn_stringbuf_t *target_path,
                     const svn_delta_edit_fns_t *editor,
                     void *edit_baton,
                     apr_pool_t *pool)
{
  void *root_baton;
  struct context c;

  if (! source_path)
    {
      return
        svn_error_create
        (SVN_ERR_FS_PATH_SYNTAX, 0, 0, pool,
         "directory delta source path is invalid");
    }

  if (! target_path)
    {
      return
        svn_error_create
        (SVN_ERR_FS_PATH_SYNTAX, 0, 0, pool,
         "directory delta target path is invalid");
    }

  {
    int is_dir;

    SVN_ERR (svn_fs_is_dir (&is_dir, source_root, source_path->data, pool));
    if (! is_dir)
      return
        svn_error_create
        (SVN_ERR_FS_NOT_DIRECTORY, 0, 0, pool,
         "directory delta source path is not a directory");

    SVN_ERR (svn_fs_is_dir (&is_dir, target_root, target_path->data, pool));
    if (! is_dir)
      return
        svn_error_create
        (SVN_ERR_FS_NOT_DIRECTORY, 0, 0, pool,
         "directory delta target path is not a directory");
  }

  /* If our target here is a revision, call set_target_revision to set
     the global target revision for our edit.  Else, whine like a baby
     because we don't want to deal with txn root targets right now.  */
  if (svn_fs_is_revision_root (target_root))
    {
      SVN_ERR (editor->set_target_revision
               (edit_baton,
                svn_fs_revision_root_revision (target_root)));
    }
  else
    {
      return
        svn_error_create
        (SVN_ERR_FS_NOT_REVISION_ROOT, 0, 0, pool,
         "directory delta target not a revision root");
    }

  /* Setup our pseudo-global structure here.  We need these variables
     throughout the deltafication process, so pass them around by
     reference to all the helper functions. */
  c.editor = editor;
  c.source_root = source_root;
  c.source_rev_diffs = source_rev_diffs;
  c.target_root = target_root;

  /* Call replace_root to get our root_baton... */
  SVN_ERR (editor->replace_root
           (edit_baton,
            get_revision_from_hash (source_rev_diffs,
                                    target_path,
                                    pool),
            &root_baton));

  /* ...and then begin the recursive directory deltafying process!  */
  SVN_ERR (delta_dirs (&c, root_baton, source_path,
                       target_path, pool));

  /* Make sure we close the root directory we opened above. */
  SVN_ERR (editor->close_directory (root_baton));

  /* Close the edit. */
  SVN_ERR (editor->close_edit (edit_baton));

  /* All's well that ends well. */
  return SVN_NO_ERROR;
}



/* Retrieving the base revision from the path/revision hash.  */


/* Look through a HASH (with paths as keys, and pointers to revision
   numbers as values) for the revision associated with the given PATH.
   Perform all necessary memory allocations in POOL.  */
static svn_revnum_t
get_revision_from_hash (apr_hash_t *hash, svn_stringbuf_t *path,
                        apr_pool_t *pool)
{
  void *val;
  svn_stringbuf_t *path_copy;
  svn_revnum_t revision = SVN_INVALID_REVNUM;

  if (! hash)
    return SVN_INVALID_REVNUM;

  /* See if this path has a revision assigned in the hash. */
  val = apr_hash_get (hash, path->data, APR_HASH_KEY_STRING);
  if (val)
    {
      revision = *((svn_revnum_t *) val);
      if (SVN_IS_VALID_REVNUM(revision))
        return revision;
    }

  /* Make a copy of our path that we can hack on. */
  path_copy = svn_string_dup (path, pool);

  /* If we haven't found a valid revision yet, and our copy of the
     path isn't empty, hack the last component off the path and see if
     *that* has a revision entry in our hash. */
  while ((! SVN_IS_VALID_REVNUM(revision))
         && (! svn_path_is_empty (path_copy, svn_path_repos_style)))
    {
      svn_path_remove_component (path_copy, svn_path_repos_style);

      val = apr_hash_get (hash, path_copy->data, APR_HASH_KEY_STRING);
      if (val)
        revision = *((svn_revnum_t *) val);
    }

  return revision;
}




/* proplist_change_fn_t property changing functions.  */


/* Call the directory property-setting function of C->editor to set
   the property NAME to given VALUE on the OBJECT passed to this
   function. */
static svn_error_t *
change_dir_prop (struct context *c, void *object,
                 svn_stringbuf_t *name, svn_stringbuf_t *value, apr_pool_t *pool)
{
  return c->editor->change_dir_prop (object, name, value);
}


/* Call the file property-setting function of C->editor to set the
   property NAME to given VALUE on the OBJECT passed to this
   function. */
static svn_error_t *
change_file_prop (struct context *c, void *object,
                  svn_stringbuf_t *name, svn_stringbuf_t *value, apr_pool_t *pool)
{
  return c->editor->change_file_prop (object, name, value);
}




/* Constructing deltas for properties of files and directories.  */


/* Generate the appropriate property editing calls to turn the
   properties of SOURCE_PATH into those of TARGET_PATH.  If
   SOURCE_PATH is NULL, treat it as if it were a file with no
   properties.  Pass OBJECT on to the editor function wrapper
   CHANGE_FN. */
static svn_error_t *
delta_proplists (struct context *c,
                 svn_stringbuf_t *source_path,
                 svn_stringbuf_t *target_path,
                 proplist_change_fn_t *change_fn,
                 void *object,
                 apr_pool_t *pool)
{
  apr_hash_t *s_props = 0;
  apr_hash_t *t_props = 0;
  apr_hash_index_t *hi;
  apr_pool_t *subpool;

  /* Make a subpool for local allocations. */
  subpool = svn_pool_create (pool);

  /* Get the source file's properties */
  if (source_path)
    SVN_ERR (svn_fs_node_proplist
             (&s_props, c->source_root, source_path->data,
              subpool));

  /* Get the target file's properties */
  if (target_path)
    SVN_ERR (svn_fs_node_proplist
             (&t_props, c->target_root, target_path->data,
              subpool));

  for (hi = apr_hash_first (t_props); hi; hi = apr_hash_next (hi))
    {
      svn_stringbuf_t *s_value, *t_value, *t_name;
      const void *key;
      void *val;
      apr_size_t klen;

      /* KEY is property name in target, VAL the value */
      apr_hash_this (hi, &key, &klen, &val);
      t_name = svn_string_ncreate (key, klen, subpool);
      t_value = val;

      /* See if this property existed in the source.  If so, and if
         the values in source and target differ, replace the value in
         target with the one in source. */
      if (s_props
          && ((s_value = apr_hash_get (s_props, key, klen)) != 0))
        {
          if (svn_string_compare (s_value, t_value))
            SVN_ERR (change_fn (c, object, t_name, t_value, subpool));

          /* Remove the property from source list so we can track
             which items have matches in the target list. */
          apr_hash_set (s_props, key, klen, NULL);
        }
      else
        {
          /* This property didn't exist in the source, so this is just
             and add. */
          SVN_ERR (change_fn (c, object, t_name, t_value, subpool));
        }
    }

  /* All the properties remaining in the source list are not present
     in the target, and so must be deleted. */
  if (s_props)
    {
      for (hi = apr_hash_first (s_props); hi; hi = apr_hash_next (hi))
        {
          svn_stringbuf_t *s_value, *s_name;
          const void *key;
          void *val;
          apr_size_t klen;

          /* KEY is property name in target, VAL the value */
          apr_hash_this (hi, &key, &klen, &val);
          s_name = svn_string_ncreate (key, klen, subpool);
          s_value = val;

          SVN_ERR (change_fn (c, object, s_name, s_value, subpool));
        }
    }

  /* Destroy local subpool. */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}




/* Constructing deltas for file constents.  */


/* Change the contents of FILE_BATON in C->editor, according to the
   text delta from DELTA_STREAM.  */
static svn_error_t *
send_text_delta (struct context *c,
                 void *file_baton,
                 svn_txdelta_stream_t *delta_stream,
                 apr_pool_t *pool)
{
  svn_txdelta_window_handler_t delta_handler;
  svn_txdelta_window_t *window;
  void *delta_handler_baton;

  /* Get a handler that will apply the delta to the file.  */
  SVN_ERR (c->editor->apply_textdelta
           (file_baton, &delta_handler, &delta_handler_baton));

  /* Read windows from the delta stream, and apply them to the file.  */
  do
    {
      SVN_ERR (svn_txdelta_next_window (&window, delta_stream));
      SVN_ERR (delta_handler (window, delta_handler_baton));
      if (window)
        svn_txdelta_free_window (window);
    }
  while (window);

  return SVN_NO_ERROR;
}


/* Make the appropriate edits on FILE_BATON to change its contents and
   properties from those in SOURCE_PATH to those in TARGET_PATH. */
static svn_error_t *
delta_files (struct context *c, void *file_baton,
             svn_stringbuf_t *source_path,
             svn_stringbuf_t *target_path,
             apr_pool_t *pool)
{
  svn_txdelta_stream_t *delta_stream;
  apr_pool_t *subpool;

  /* Make a subpool for local allocations. */
  subpool = svn_pool_create (pool);

  /* Compare the files' property lists.  */
  SVN_ERR (delta_proplists (c, source_path, target_path,
                            change_file_prop, file_baton, subpool));

  if (source_path)
    {
      /* Get a delta stream turning SOURCE_PATH's contents into
         TARGET_PATH's contents.  */
      SVN_ERR (svn_fs_get_file_delta_stream
               (&delta_stream,
                c->source_root, source_path->data,
                c->target_root, target_path->data,
                subpool));
    }
  else
    {
      /* Get a delta stream turning an empty file into one having
         TARGET_PATH's contents.  */
      SVN_ERR (svn_fs_get_file_delta_stream
               (&delta_stream, 0, 0,
                c->target_root, target_path->data, subpool));
    }

  SVN_ERR (send_text_delta (c, file_baton, delta_stream, subpool));

  /* Cleanup. */
  svn_txdelta_free (delta_stream);
  svn_pool_destroy (subpool);

  return 0;
}




/* Generic directory deltafication routines.  */


/* Emit a delta to delete the entry named TARGET_ENTRY from DIR_BATON.  */
static svn_error_t *
delete (struct context *c,
        void *dir_baton,
        svn_stringbuf_t *target_entry,
        apr_pool_t *pool)
{
  return c->editor->delete_entry (target_entry, dir_baton);
}


/* Emit a delta to create the entry named TARGET_ENTRY in the
   directory TARGET_PARENT.  If SOURCE_PARENT and SOURCE_ENTRY are
   valid, use them to determine the copyfrom args in the editor's add
   calls.  Pass DIR_BATON through to editor functions that require it.  */
static svn_error_t *
add_file_or_dir (struct context *c, void *dir_baton,
                 svn_stringbuf_t *target_parent,
                 svn_stringbuf_t *target_entry,
                 svn_stringbuf_t *source_parent,
                 svn_stringbuf_t *source_entry,
                 apr_pool_t *pool)
{
  int is_dir;
  svn_stringbuf_t *target_full_path = 0;
  svn_stringbuf_t *source_full_path = 0;
  svn_revnum_t base_revision = SVN_INVALID_REVNUM;

  if (!target_parent || !target_entry)
    abort();

  /* Get the target's full path */
  target_full_path = svn_string_dup (target_parent, pool);
  svn_path_add_component
    (target_full_path, target_entry, svn_path_repos_style);

  /* Is the target a file or a directory?  */
  SVN_ERR (svn_fs_is_dir (&is_dir, c->target_root,
                          target_full_path->data, pool));

  if (source_parent && source_entry)
    {
      /* Get the source's full path */
      source_full_path = svn_string_dup (source_parent, pool);
      svn_path_add_component
        (source_full_path, source_entry, svn_path_repos_style);

      /* Get the base revision for the entry from the hash. */
      base_revision = get_revision_from_hash (c->source_rev_diffs,
                                              source_full_path,
                                              pool);

      if (! SVN_IS_VALID_REVNUM(base_revision))
        return
          svn_error_create
          (SVN_ERR_FS_NO_SUCH_REVISION, 0, 0, pool,
           "unable to ascertain base revision for source path");
    }

  if (is_dir)
    {
      void *subdir_baton;

      SVN_ERR (c->editor->add_directory
               (target_entry, dir_baton,
                source_full_path, base_revision, &subdir_baton));
      SVN_ERR (delta_dirs (c, subdir_baton, source_full_path,
                           target_full_path, pool));
      SVN_ERR (c->editor->close_directory (subdir_baton));
    }
  else
    {
      void *file_baton;

      SVN_ERR (c->editor->add_file
               (target_entry, dir_baton,
                source_full_path, base_revision, &file_baton));
      SVN_ERR (delta_files (c, file_baton, source_full_path,
                            target_full_path, pool));
      SVN_ERR (c->editor->close_file (file_baton));
    }

  return SVN_NO_ERROR;
}


/* Modify the directory TARGET_PARENT by replacing its entry named
   TARGET_ENTRY with the SOURCE_ENTRY found in SOURCE_PARENT.  Pass
   DIR_BATON through to editor functions that require it. */
static svn_error_t *
replace_file_or_dir (struct context *c,
                     void *dir_baton,
                     svn_stringbuf_t *target_parent,
                     svn_stringbuf_t *target_entry,
                     svn_stringbuf_t *source_parent,
                     svn_stringbuf_t *source_entry,
                     apr_pool_t *pool)
{
  int is_dir;
  svn_stringbuf_t *source_full_path = 0;
  svn_stringbuf_t *target_full_path = 0;
  svn_revnum_t base_revision = SVN_INVALID_REVNUM;

  if (!target_parent || !target_entry)
    abort();

  if (!source_parent || !source_entry)
    abort();

  /* Get the target's full path */
  target_full_path = svn_string_dup (target_parent, pool);
  svn_path_add_component
    (target_full_path, target_entry, svn_path_repos_style);

  /* Is the target a file or a directory?  */
  SVN_ERR (svn_fs_is_dir (&is_dir, c->target_root,
                          target_full_path->data, pool));

  /* Get the source's full path */
  source_full_path = svn_string_dup (source_parent, pool);
  svn_path_add_component
    (source_full_path, source_entry, svn_path_repos_style);

  /* Get the base revision for the entry from the hash. */
  base_revision = get_revision_from_hash (c->source_rev_diffs,
                                          source_full_path,
                                          pool);

  if (! SVN_IS_VALID_REVNUM(base_revision))
    return
      svn_error_create
      (SVN_ERR_FS_NO_SUCH_REVISION, 0, 0, pool,
       "unable to ascertain base revision for source path");

  if (is_dir)
    {
      void *subdir_baton;

      SVN_ERR (c->editor->replace_directory
               (target_entry, dir_baton, base_revision, &subdir_baton));
      SVN_ERR (delta_dirs
               (c, subdir_baton, source_full_path, target_full_path, pool));
      SVN_ERR (c->editor->close_directory (subdir_baton));
    }
  else
    {
      void *file_baton;

      SVN_ERR (c->editor->replace_file
               (target_entry, dir_baton, base_revision, &file_baton));
      SVN_ERR (delta_files
               (c, file_baton, source_full_path, target_full_path, pool));
      SVN_ERR (c->editor->close_file (file_baton));
    }

  return SVN_NO_ERROR;
}


#if 0   /* commented out to avoid unused function warning */
/* Do a `replace' edit in DIR_BATON, replacing the entry named
   T_ENTRY->name in the directory TARGET_PARENT with the closest
   related node available in SOURCE_PARENT.  If no relative can be
   found, simply delete in the entry from TARGET_PARENT, and then
   re-add the new one. */
static svn_error_t *
find_nearest_entry (svn_fs_dirent_t **s_entry,
                    int *distance,
                    struct context *c,
                    svn_stringbuf_t *source_parent,
                    svn_stringbuf_t *target_parent,
                    svn_fs_dirent_t *t_entry,
                    apr_pool_t *pool)
{
  apr_hash_t *s_entries;
  apr_hash_index_t *hi;
  int best_distance = -1;
  svn_fs_dirent_t *best_entry = NULL;
  svn_stringbuf_t *source_full_path;
  svn_stringbuf_t *target_full_path;
  svn_stringbuf_t *target_entry;
  int t_is_dir;
  apr_pool_t *subpool;

  /* Make a subpool for local allocations */
  subpool = svn_pool_create (pool);

  /* If there's no source to search, return a failed ancestor hunt. */
  source_full_path = svn_string_create ("", subpool);
  if (! source_parent)
    {
      *s_entry = 0;
      *distance = -1;
      svn_pool_destroy (subpool);
      return SVN_NO_ERROR;
    }

  /* Get the list of entries in source.  Note that we are using the
     pool that was passed in instead of the subpool...we're returning
     a reference to an item in this hash, and it would suck to blow it
     away before our caller gets a chance to see it.  */
  SVN_ERR (svn_fs_dir_entries (&s_entries, c->source_root,
                               source_parent->data, pool));

  target_full_path = svn_string_dup (target_parent, subpool);
  target_entry = svn_string_create (t_entry->name, subpool);
  svn_path_add_component (target_full_path, target_entry,
                          svn_path_repos_style);

  /* Is the target a file or a directory?  */
  SVN_ERR (svn_fs_is_dir (&t_is_dir, c->target_root,
                          target_full_path->data, subpool));

  /* Find the closest relative to TARGET_ENTRY in SOURCE.

     In principle, a replace operation can choose the ancestor from
     anywhere in the delta's whole source tree.  In this
     implementation, we only search SOURCE for possible ancestors.
     This will need to improve, so we can find the best ancestor, no
     matter where it's hidden away in the source tree.  */
  for (hi = apr_hash_first (s_entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      apr_size_t klen;
      int this_distance;
      svn_fs_dirent_t *this_entry;
      int s_is_dir;

      /* KEY will be the entry name in source, VAL the dirent */
      apr_hash_this (hi, &key, &klen, &val);
      this_entry = val;

      svn_string_set (source_full_path, source_parent->data);
      svn_path_add_component (source_full_path,
                              svn_string_create (this_entry->name, subpool),
                              svn_path_repos_style);

      /* Is this entry a file or a directory?  */
      SVN_ERR (svn_fs_is_dir (&s_is_dir, c->source_root,
                              source_full_path->data, subpool));

      /* If we aren't looking at the same node type, skip this
         entry. */
      if ((s_is_dir && (! t_is_dir)) || ((! s_is_dir) && t_is_dir))
        continue;

      /* Find the distance between the target entry and this source
         entry.  This returns -1 if they're completely unrelated.
         Here we're using ID distance as an approximation for delta
         size.  */
      this_distance = svn_fs_id_distance (t_entry->id, this_entry->id);

      /* If these nodes are completely unrelated, move along. */
      if (this_distance == -1)
        continue;

      /* If this is the first related node we've found, or just a
         closer node than previously discovered, update our
         best_distance tracker. */
      if ((best_distance == -1) || (this_distance < best_distance))
        {
          best_distance = this_distance;
          best_entry = this_entry;
        }
    }

  /* If our best distance is still reflects no ancestry, return a NULL
     entry to the caller, else return the best entry we found. */
  *s_entry = ((*distance = best_distance) == -1) ? 0 : best_entry;

  /* Destroy local allocation subpool. */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}
#endif /* 0 */


/* Emit deltas to turn SOURCE_PATH into TARGET_PATH.  Assume that
   DIR_BATON represents the directory we're constructing to the editor
   in the context C.  */
static svn_error_t *
delta_dirs (struct context *c,
            void *dir_baton,
            svn_stringbuf_t *source_path,
            svn_stringbuf_t *target_path,
            apr_pool_t *pool)
{
  apr_hash_t *s_entries = 0, *t_entries = 0;
  apr_hash_index_t *hi;
  svn_stringbuf_t *target_name = svn_string_create ("", pool);
  apr_pool_t *subpool;

  /* Compare the property lists.  */
  SVN_ERR (delta_proplists (c, source_path, target_path,
                            change_dir_prop, dir_baton, pool));

  /* Get the list of entries in each of source and target.  */
  if (target_path)
    {
      SVN_ERR (svn_fs_dir_entries (&t_entries, c->target_root,
                                   target_path->data, pool));
    }
  else
    {
      /* Return a viscious error. */
      abort();
    }

  if (source_path)
    {
      SVN_ERR (svn_fs_dir_entries (&s_entries, c->source_root,
                                   source_path->data, pool));
    }

  /* Make a subpool for local allocations. */
  subpool = svn_pool_create (pool);

  /* Loop over the hash of entries in the target, searching for its
     partner in the source.  If we find the matching partner entry,
     use editor calls to replace the one in target with a new version
     if necessary, then remove that entry from the source entries
     hash.  If we can't find a related node in the source, we use
     editor calls to add the entry as a new item in the target.
     Having handled all the entries that exist in target, any entries
     still remaining the source entries hash represent entries that no
     longer exist in target.  Use editor calls to delete those entries
     from the target tree. */
  for (hi = apr_hash_first (t_entries); hi; hi = apr_hash_next (hi))
    {
      svn_fs_dirent_t *s_entry, *t_entry;
      const void *key;
      void *val;
      apr_size_t klen;

      /* KEY is the entry name in target, VAL the dirent */
      apr_hash_this (hi, &key, &klen, &val);
      t_entry = val;

      svn_string_set (target_name, t_entry->name);

      /* Can we find something with the same name in the source
         entries hash? */
      if (s_entries
          && ((s_entry = apr_hash_get (s_entries, key, klen)) != 0))
        {
          int distance;

          /* Check the distance between the ids.

             0 means they are the same id, and this is a noop.

             -1 means they are unrelated, so try to find an ancestor
             elsewhere in the directory.  Theoretically, using an
             ancestor as a baseline will reduce the size of the deltas.

             Any other positive value means the nodes are related
             through ancestry, so go ahead and do the replace
             directly.  */
          distance = svn_fs_id_distance (s_entry->id, t_entry->id);
          if (distance == 0)
            {
              /* no-op */
            }
          else if (distance == -1)
            {
#if SVN_FS_SUPPORT_COPY_FROM_ARGS
              svn_fs_dirent_t *best_entry;
              int best_distance;

              SVN_ERR (find_nearest_entry (&best_entry, &best_distance,
                                           c, source_path,
                                           target_path, t_entry));

              if (best_distance == -1)
#endif
                {
                  /* We found no ancestral match at all.  Delete this
                     entry and create a new one from scratch. */
                  SVN_ERR (delete (c, dir_baton, target_name, subpool));
                  SVN_ERR (add_file_or_dir
                           (c, dir_baton, target_path, target_name,
                            0, 0, subpool));
                }
#if SVN_FS_SUPPORT_COPY_FROM_ARGS
              else
                {
                  /* We found a relative.  It *shouldn't* have the
                     same name (since this case gets caught in the
                     first distance check above), but does it really
                     matter? */
                  SVN_ERR (replace_file_or_dir
                           (c, dir_baton,
                            target_path,
                            target_name,
                            source_path,
                            svn_string_create (best_entry->name, subpool),
                            subpool));
                }
#endif
            }
          else
            {
              SVN_ERR (replace_file_or_dir
                       (c, dir_baton,
                        target_path,
                        target_name,
                        source_path,
                        svn_string_create (s_entry->name, subpool),
                        subpool));
            }

          /*  Remove the entry from the source_hash. */
          apr_hash_set (s_entries, key, APR_HASH_KEY_STRING, NULL);
        }
      else
        {
#if SVN_FS_SUPPORT_COPY_FROM_ARGS
          /* We didn't find an entry with this name in the source
             entries hash.  This must be something new that needs to
             be added.  But let's first check to see if we can find an
             ancestor from which to copy this new entry. */
          svn_fs_dirent_t *best_entry;
          int best_distance;

          SVN_ERR (find_nearest_entry (&best_entry, &best_distance,
                                       c, source_path,
                                       target_path, t_entry, subpool));

          /* Add (with history if we found an ancestor) this new
             entry. */
          if (best_distance == -1)
#endif
            SVN_ERR (add_file_or_dir
                     (c, dir_baton, target_path, target_name, 0, 0, subpool));
#if SVN_FS_SUPPORT_COPY_FROM_ARGS
          else
            SVN_ERR (add_file_or_dir
                     (c, dir_baton,
                      target_path,
                      target_name,
                      source_path,
                      svn_string_create (best_entry->name, subpool),
                      subpool));
#endif
        }
      /* Clear out our subpool for the next iteration... */
      svn_pool_clear (subpool);
    }

  /* All that is left in the source entries hash are things that need
     to be deleted.  Delete them.  */
  if (s_entries)
    {
      for (hi = apr_hash_first (s_entries); hi; hi = apr_hash_next (hi))
        {
          svn_fs_dirent_t *s_entry;
          const void *key;
          void *val;
          apr_size_t klen;

          /* KEY is the entry name in source, VAL the dirent */
          apr_hash_this (hi, &key, &klen, &val);
          s_entry = val;

          SVN_ERR (delete (c, dir_baton,
                           svn_string_create (s_entry->name, subpool),
                           subpool));
        }
    }

  /* Destroy local allocation subpool. */
  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}




/*
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
