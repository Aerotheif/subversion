/*
 *  dump_editor.c: The svn_delta_editor_t editor used by svnrdump
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

#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_repos.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_dirent_uri.h"

#include "svnrdump.h"
#include "dump_editor.h"

#define ARE_VALID_COPY_ARGS(p,r) ((p) && SVN_IS_VALID_REVNUM(r))

/* Make a directory baton to represent the directory at path (relative
 * to the edit_baton).
 *
 * COPYFROM_PATH/COPYFROM_REV are the path/revision against which this
 * directory should be compared for changes. If the copyfrom
 * information is valid, the directory will be compared against its
 * copy source.
 *
 * PARENT_DIR_BATON is the directory baton of this directory's parent,
 * or NULL if this is the top-level directory of the edit.  ADDED
 * indicates if this directory is newly added in this revision.
 * Perform all allocations in POOL.  */
static struct dir_baton *
make_dir_baton(const char *path,
               const char *copyfrom_path,
               svn_revnum_t copyfrom_rev,
               void *edit_baton,
               void *parent_dir_baton,
               svn_boolean_t added,
               apr_pool_t *pool)
{
  struct dump_edit_baton *eb = edit_baton;
  struct dir_baton *pb = parent_dir_baton;
  struct dir_baton *new_db = apr_pcalloc(pool, sizeof(*new_db));
  const char *abspath;

  /* Disallow a path relative to nothing. */
  SVN_ERR_ASSERT_NO_RETURN(!path || pb);

  /* Construct the full path of this node. */
  if (pb)
    abspath = svn_dirent_join_many(pool, "/", path, NULL);
  else
    abspath = apr_pstrdup(pool, "/");

  /* Remove leading slashes from copyfrom paths. */
  if (copyfrom_path && strcmp(copyfrom_path, "/"))
    copyfrom_path = ((*copyfrom_path == '/') ?
                      copyfrom_path + 1 : copyfrom_path);

  new_db->eb = eb;
  new_db->parent_dir_baton = pb;
  new_db->abspath = abspath;
  new_db->copyfrom_path = copyfrom_path ?
    apr_pstrdup(pool, copyfrom_path) : NULL;
  new_db->copyfrom_rev = copyfrom_rev;
  new_db->added = added;
  new_db->written_out = FALSE;
  new_db->deleted_entries = apr_hash_make(pool);

  return new_db;
}
/*
 * Write out a node record for PATH of type KIND under EB->FS_ROOT.
 * ACTION describes what is happening to the node (see enum
 * svn_node_action). Write record to writable EB->STREAM, using
 * EB->BUFFER to write in chunks.
 *
 * If the node was itself copied, IS_COPY is TRUE and the
 * path/revision of the copy source are in COPYFROM_PATH/COPYFROM_REV.
 * If IS_COPY is FALSE, yet COPYFROM_PATH/COPYFROM_REV are valid, this
 * node is part of a copied subtree.
 */
static svn_error_t *
dump_node(struct dump_edit_baton *eb,
          const char *path,    /* an absolute path. */
          svn_node_kind_t kind,
          enum svn_node_action action,
          svn_boolean_t is_copy,
          const char *copyfrom_path,
          svn_revnum_t copyfrom_rev,
          apr_pool_t *pool)
{
  /* Remove leading slashes from path and copyfrom_path */
  if (path && strcmp(path, "/"))
    path = ((*path == '/') ? path + 1 : path);

  if (copyfrom_path && strcmp(copyfrom_path, "/"))
    copyfrom_path = ((*copyfrom_path == '/') ?
                      copyfrom_path + 1 : copyfrom_path);

  /* Node-path: commons/STATUS */
  SVN_ERR(svn_stream_printf(eb->stream, pool,
          SVN_REPOS_DUMPFILE_NODE_PATH ": %s\n", path));

  /* Node-kind: file */
  if (kind == svn_node_file)
    SVN_ERR(svn_stream_printf(eb->stream, pool,
                              SVN_REPOS_DUMPFILE_NODE_KIND ": file\n"));
  else if (kind == svn_node_dir)
    SVN_ERR(svn_stream_printf(eb->stream, pool,
                              SVN_REPOS_DUMPFILE_NODE_KIND ": dir\n"));


  switch (action) {
  /* Write the appropriate Node-action header */

  case svn_node_action_change:
    SVN_ERR(svn_stream_printf(eb->stream, pool,
                              SVN_REPOS_DUMPFILE_NODE_ACTION
                              ": change\n"));
    break;

  case svn_node_action_replace:
    if (!is_copy) {
      /* Node-action: replace */
      SVN_ERR(svn_stream_printf(eb->stream, pool,
                                SVN_REPOS_DUMPFILE_NODE_ACTION
                                ": replace\n"));

      eb->dump_props_pending = TRUE;
      break;
    }
    /* More complex case: is_copy is true, and copyfrom_path/
       copyfrom_rev are present: delete the original, and then re-add
       it */

    SVN_ERR(svn_stream_printf(eb->stream, pool,
                              SVN_REPOS_DUMPFILE_NODE_ACTION
                              ": delete\n\n"));

    /* Recurse: Print an additional add-with-history record. */
    SVN_ERR(dump_node(eb, path, kind, svn_node_action_add,
                      is_copy, copyfrom_path, copyfrom_rev, pool));

    /* We can leave this routine quietly now, don't need to dump any
       content; that was already done in the second record. */
    eb->dump_props = FALSE;
    break;

  case svn_node_action_delete:
    SVN_ERR(svn_stream_printf(eb->stream, pool,
                              SVN_REPOS_DUMPFILE_NODE_ACTION
                              ": delete\n"));

    /* We can leave this routine quietly now, don't need to dump
       any content. */
    SVN_ERR(svn_stream_printf(eb->stream, pool, "\n\n"));
    eb->dump_props = FALSE;
    break;

  case svn_node_action_add:
    SVN_ERR(svn_stream_printf(eb->stream, pool,
                              SVN_REPOS_DUMPFILE_NODE_ACTION ": add\n"));

    if (!is_copy) {
      /* eb->dump_props_pending for files is handled in close_file
         which is called immediately.  However, directories are not
         closed until all the work inside them has been done;
         eb->dump_props_pending for directories is handled in all the
         functions that can possibly be called after add_directory:
         add_directory, open_directory, delete_entry, close_directory,
         add_file, open_file. change_dir_prop is a special case. */

      eb->dump_props_pending = TRUE;
      break;
    }

    SVN_ERR(svn_stream_printf(eb->stream, pool,
                              SVN_REPOS_DUMPFILE_NODE_COPYFROM_REV
                              ": %ld\n"
                              SVN_REPOS_DUMPFILE_NODE_COPYFROM_PATH
                              ": %s\n",
                              copyfrom_rev, copyfrom_path));

    /* Ugly hack: If a directory was copied from a previous revision,
       nothing else can be done, and close_file won't be called to
       write two blank lines. Write them here otherwise the `svnadmin
       load` parser will fail. */
    if (kind == svn_node_dir)
      SVN_ERR(svn_stream_printf(eb->stream, pool, "\n\n"));

    break;
  }

  /* Dump property headers */
  SVN_ERR(dump_props(eb, &(eb->dump_props), FALSE, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *open_root(void *edit_baton,
            svn_revnum_t base_revision,
            apr_pool_t *pool,
            void **root_baton)
{
  struct dump_edit_baton *eb = edit_baton;
  /* Allocate a special pool for the edit_baton to avoid pool
     lifetime issues */
  eb->pool = svn_pool_create(pool);
  eb->properties = apr_hash_make(eb->pool);
  eb->del_properties = apr_hash_make(eb->pool);
  eb->propstring = svn_stringbuf_create("", eb->pool);

  *root_baton = make_dir_baton(NULL, NULL, SVN_INVALID_REVNUM,
                               edit_baton, NULL, FALSE, pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  struct dir_baton *pb = parent_baton;
  const char *mypath = apr_pstrdup(pool, path);

  /* Some pending properties to dump? */
  SVN_ERR(dump_props(pb->eb, &(pb->eb->dump_props_pending), TRUE, pool));

  /* Add this path to the deleted_entries of the parent directory
     baton. */
  apr_hash_set(pb->deleted_entries, mypath, APR_HASH_KEY_STRING, pb);

  return SVN_NO_ERROR;
}

static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_rev,
              apr_pool_t *pool,
              void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  void *val;
  struct dir_baton *new_db
    = make_dir_baton(path, copyfrom_path, copyfrom_rev, pb->eb, pb, TRUE, pool);
  svn_boolean_t is_copy;

  /* Some pending properties to dump? */
  SVN_ERR(dump_props(pb->eb, &(pb->eb->dump_props_pending), TRUE, pool));

  /* This might be a replacement -- is the path already deleted? */
  val = apr_hash_get(pb->deleted_entries, path, APR_HASH_KEY_STRING);

  /* Detect an add-with-history */
  is_copy = ARE_VALID_COPY_ARGS(copyfrom_path, copyfrom_rev);

  /* Dump the node */
  SVN_ERR(dump_node(pb->eb, path,
                    svn_node_dir,
                    val ? svn_node_action_replace : svn_node_action_add,
                    is_copy,
                    is_copy ? copyfrom_path : NULL,
                    is_copy ? copyfrom_rev : SVN_INVALID_REVNUM,
                    pool));

  if (val)
    /* Delete the path, it's now been dumped */
    apr_hash_set(pb->deleted_entries, path, APR_HASH_KEY_STRING, NULL);

  new_db->written_out = TRUE;

  *child_baton = new_db;
  return SVN_NO_ERROR;
}

static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  struct dir_baton *pb = parent_baton;
  struct dir_baton *new_db;
  const char *copyfrom_path = NULL;
  svn_revnum_t copyfrom_rev = SVN_INVALID_REVNUM;

  /* Some pending properties to dump? */
  SVN_ERR(dump_props(pb->eb, &(pb->eb->dump_props_pending), TRUE, pool));

  /* If the parent directory has explicit comparison path and rev,
     record the same for this one. */
  if (pb && ARE_VALID_COPY_ARGS(pb->copyfrom_path, pb->copyfrom_rev)) {
    copyfrom_path = svn_dirent_join_many(pool, pb->copyfrom_path,
                                         svn_relpath_basename(path, pool),
                                         NULL);
    copyfrom_rev = pb->copyfrom_rev;
  }

  new_db = make_dir_baton(path, copyfrom_path, copyfrom_rev, pb->eb, pb, FALSE, pool);
  *child_baton = new_db;
  return SVN_NO_ERROR;
}

static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  struct dir_baton *db = dir_baton;
  struct dump_edit_baton *eb = db->eb;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);

  /* Some pending properties to dump? */
  SVN_ERR(dump_props(eb, &(eb->dump_props_pending), TRUE, pool));

  /* Dump the directory entries */
  for (hi = apr_hash_first(pool, db->deleted_entries); hi;
       hi = apr_hash_next(hi)) {
    const void *key;
    const char *path;
    apr_hash_this(hi, &key, NULL, NULL);
    path = key;

    svn_pool_clear(iterpool);

    SVN_ERR(dump_node(db->eb, path, svn_node_unknown, svn_node_action_delete,
                      FALSE, NULL, SVN_INVALID_REVNUM, iterpool));
  }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_rev,
         apr_pool_t *pool,
         void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  void *val;
  svn_boolean_t is_copy;

  /* Some pending properties to dump? */
  SVN_ERR(dump_props(pb->eb, &(pb->eb->dump_props_pending), TRUE, pool));

  /* This might be a replacement -- is the path already deleted? */
  val = apr_hash_get(pb->deleted_entries, path, APR_HASH_KEY_STRING);

  /* Detect add-with-history. */
  is_copy = ARE_VALID_COPY_ARGS(copyfrom_path, copyfrom_rev);

  /* Dump the node. */
  SVN_ERR(dump_node(pb->eb, path,
                    svn_node_file,
                    val ? svn_node_action_replace : svn_node_action_add,
                    is_copy,
                    is_copy ? copyfrom_path : NULL,
                    is_copy ? copyfrom_rev : SVN_INVALID_REVNUM,
                    pool));

  if (val)
    /* delete the path, it's now been dumped. */
    apr_hash_set(pb->deleted_entries, path, APR_HASH_KEY_STRING, NULL);

  /* Build a nice file baton to pass to change_file_prop and
     apply_textdelta */
  *file_baton = pb->eb;

  return SVN_NO_ERROR;
}

static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t ancestor_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  struct dir_baton *pb = parent_baton;
  const char *copyfrom_path = NULL;
  svn_revnum_t copyfrom_rev = SVN_INVALID_REVNUM;
  apr_array_header_t *compose_path;

  /* Some pending properties to dump? */
  SVN_ERR(dump_props(pb->eb, &(pb->eb->dump_props_pending), TRUE, pool));

  compose_path = apr_array_make(pool, 2, sizeof(const char *));

  /* If the parent directory has explicit copyfrom path and rev,
     record the same for this one. */
  if (pb && ARE_VALID_COPY_ARGS(pb->copyfrom_path, pb->copyfrom_rev)) {
    APR_ARRAY_PUSH(compose_path, const char *) = pb->copyfrom_path;
    APR_ARRAY_PUSH(compose_path, const char *) =
      svn_relpath_basename(path, pool);
    copyfrom_path = svn_path_compose(compose_path, pool);
    copyfrom_rev = pb->copyfrom_rev;
  }

  SVN_ERR(dump_node(pb->eb, path, svn_node_file, svn_node_action_change,
                    FALSE, copyfrom_path, copyfrom_rev, pool));

  /* Build a nice file baton to pass to change_file_prop and
     apply_textdelta */
  *file_baton = pb->eb;

  return SVN_NO_ERROR;
}

static svn_error_t *
change_dir_prop(void *parent_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  struct dir_baton *db = parent_baton;

  if (svn_property_kind(NULL, name) != svn_prop_regular_kind)
    return SVN_NO_ERROR;

  value ? apr_hash_set(db->eb->properties, apr_pstrdup(pool, name),
                       APR_HASH_KEY_STRING, svn_string_dup(value, pool)) :
    apr_hash_set(db->eb->del_properties, apr_pstrdup(pool, name),
                 APR_HASH_KEY_STRING, (void *)0x1);

  if (! db->written_out) {
  /* If db->written_out is set, it means that the node information
     corresponding to this directory has already been written: don't
     do anything; dump_props_pending will take care of dumping the
     props. If it not, dump the node itself before dumping the
     props. */

    SVN_ERR(dump_node(db->eb, db->abspath, svn_node_dir,
                      svn_node_action_change, FALSE, db->copyfrom_path,
                      db->copyfrom_rev, pool));

    SVN_ERR(dump_props(db->eb, NULL, TRUE, pool));
    db->written_out = TRUE;
  }
  return SVN_NO_ERROR;
}

static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  struct dump_edit_baton *eb = file_baton;

  if (svn_property_kind(NULL, name) != svn_prop_regular_kind)
    return SVN_NO_ERROR;

  apr_hash_set(eb->properties, apr_pstrdup(pool, name),
               APR_HASH_KEY_STRING, value ?
               svn_string_dup(value, pool): (void *)0x1);
  /* Dump the property headers and wait; close_file might need
     to write text headers too depending on whether
     apply_textdelta is called */
  eb->dump_props_pending = TRUE;

  return SVN_NO_ERROR;
}

static svn_error_t *
window_handler(svn_txdelta_window_t *window, void *baton)
{
  struct handler_baton *hb = baton;
  struct dump_edit_baton *eb = hb->eb;
  static svn_error_t *err;

  err = hb->apply_handler(window, hb->apply_baton);
  if (window != NULL && !err)
    return SVN_NO_ERROR;

  if (err)
    SVN_ERR(err);

  /* Write information about the filepath to hb->eb */
  eb->delta_abspath = apr_pstrdup(eb->pool,
          hb->delta_abspath);

  /* Cleanup */
  SVN_ERR(svn_io_file_close(hb->delta_file, hb->pool));
  SVN_ERR(svn_stream_close(hb->delta_filestream));
  svn_pool_destroy(hb->pool);
  return SVN_NO_ERROR;
}

static svn_error_t *
apply_textdelta(void *file_baton, const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  struct dump_edit_baton *eb = file_baton;
  apr_status_t apr_err;
  const char *tempdir;

  /* Custom handler_baton allocated in a separate pool */
  apr_pool_t *handler_pool = svn_pool_create(pool);
  struct handler_baton *hb = apr_pcalloc(handler_pool, sizeof(*hb));
  hb->pool = handler_pool;
  hb->eb = eb;

  /* Use a temporary file to measure the text-content-length */
  SVN_ERR(svn_io_temp_dir(&tempdir, hb->pool));

  hb->delta_abspath = svn_dirent_join(tempdir, "XXXXXX", hb->pool);
  apr_err = apr_file_mktemp(&(hb->delta_file), hb->delta_abspath,
          APR_CREATE | APR_READ | APR_WRITE | APR_EXCL,
          hb->pool);
  if (apr_err != APR_SUCCESS)
    SVN_ERR(svn_error_wrap_apr(apr_err, NULL));

  hb->delta_filestream = svn_stream_from_aprfile2(hb->delta_file, TRUE, hb->pool);

  /* Prepare to write the delta to the temporary file. */
  svn_txdelta_to_svndiff2(&(hb->apply_handler), &(hb->apply_baton),
                          hb->delta_filestream, 0, hb->pool);
  eb->dump_text = TRUE;

  /* The actual writing takes place when this function has
     finished. Set handler and handler_baton now so for
     window_handler() */
  *handler = window_handler;
  *handler_baton = hb;

  return SVN_NO_ERROR;
}

static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  struct dump_edit_baton *eb = file_baton;
  apr_file_t *delta_file;
  svn_stream_t *delta_filestream;
  apr_finfo_t *info = apr_pcalloc(pool, sizeof(apr_finfo_t));

  /* Some pending properties to dump? */
  SVN_ERR(dump_props(eb, &(eb->dump_props_pending), FALSE, pool));

  /* The prop headers have already been dumped in dump_node; now dump
     the text headers. */
  if (eb->dump_text) {
    /* Text-delta: true */
    SVN_ERR(svn_stream_printf(eb->stream, pool,
            SVN_REPOS_DUMPFILE_TEXT_DELTA
            ": true\n"));

    SVN_ERR(svn_io_stat(info, eb->delta_abspath, APR_FINFO_SIZE, pool));

    /* Text-content-length: 39 */
    SVN_ERR(svn_stream_printf(eb->stream, pool,
            SVN_REPOS_DUMPFILE_TEXT_CONTENT_LENGTH
            ": %lu\n",
            (unsigned long)info->size));
    /* Text-content-md5: 82705804337e04dcd0e586bfa2389a7f */
    SVN_ERR(svn_stream_printf(eb->stream, pool,
            SVN_REPOS_DUMPFILE_TEXT_CONTENT_MD5
            ": %s\n",
            text_checksum));
  }

  /* Content-length: 1549 */
  /* If both text and props are absent, skip this header */
  if (eb->dump_props || eb->dump_props_pending)
    SVN_ERR(svn_stream_printf(eb->stream, pool,
            SVN_REPOS_DUMPFILE_CONTENT_LENGTH
            ": %ld\n\n",
            (unsigned long)info->size + eb->propstring->len));
  else if (eb->dump_text)
    SVN_ERR(svn_stream_printf(eb->stream, pool,
            SVN_REPOS_DUMPFILE_CONTENT_LENGTH
            ": %ld\n\n",
            (unsigned long)info->size));

  /* Dump the props; the propstring should have already been
     written in dump_node or above */
  if (eb->dump_props || eb->dump_props_pending) {
    SVN_ERR(svn_stream_write(eb->stream, eb->propstring->data,
           &(eb->propstring->len)));

    /* Cleanup */
    eb->dump_props = eb->dump_props_pending = FALSE;
    svn_hash__clear(eb->properties, pool);
    svn_hash__clear(eb->del_properties, pool);
  }

  /* Dump the text */
  if (eb->dump_text) {
    /* Open the temporary file, map it to a stream, copy
       the stream to eb->stream, close and delete the
       file */
    SVN_ERR(svn_io_file_open(&delta_file, eb->delta_abspath,APR_READ,
           0600,pool));
    delta_filestream = svn_stream_from_aprfile2(delta_file, TRUE, pool);
    SVN_ERR(svn_stream_copy3(delta_filestream, eb->stream, NULL, NULL, pool));

    /* Cleanup */
    SVN_ERR(svn_io_file_close(delta_file, pool));
    SVN_ERR(svn_stream_close(delta_filestream));
    SVN_ERR(svn_io_remove_file2(eb->delta_abspath, TRUE, pool));
    eb->dump_text = FALSE;
  }

  SVN_ERR(svn_stream_printf(eb->stream, pool, "\n\n"));

  return SVN_NO_ERROR;
}

static svn_error_t *
close_edit(void *edit_baton, apr_pool_t *pool)
{
  struct dump_edit_baton *eb = edit_baton;
  svn_pool_destroy(eb->pool);

  return SVN_NO_ERROR;
}

svn_error_t *
get_dump_editor(const svn_delta_editor_t **editor,
                void **edit_baton,
                apr_pool_t *pool)
{
  struct dump_edit_baton *eb;
  svn_delta_editor_t *de;

  eb = apr_pcalloc(pool, sizeof(struct dump_edit_baton));
  SVN_ERR(svn_stream_for_stdout(&(eb->stream), pool));
  de = svn_delta_default_editor(pool);

  de->open_root = open_root;
  de->delete_entry = delete_entry;
  de->add_directory = add_directory;
  de->open_directory = open_directory;
  de->close_directory = close_directory;
  de->change_dir_prop = change_dir_prop;
  de->change_file_prop = change_file_prop;
  de->apply_textdelta = apply_textdelta;
  de->add_file = add_file;
  de->open_file = open_file;
  de->close_file = close_file;
  de->close_edit = close_edit;

  /* Set the edit_baton and editor. */
  *edit_baton = eb;
  *editor = de;

  return SVN_NO_ERROR;
}
