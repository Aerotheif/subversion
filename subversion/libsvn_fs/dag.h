/* dag.h : DAG-like interface filesystem, private to libsvn_fs
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

#ifndef SVN_LIBSVN_FS_DAG_H
#define SVN_LIBSVN_FS_DAG_H

#include "svn_fs.h"
#include "db.h"
#include "skel.h"
#include "trail.h"

/* The interface in this file provides all the essential filesystem
   operations, but exposes the filesystem's DAG structure.  This makes
   it simpler to implement than the public interface, since the client
   has to understand and cope with shared structure directly as it
   appears in the database.  However, it's still a self-consistent set
   of invariants to maintain, making it (hopefully) a useful interface
   boundary.

   The fundamental differences between this interface and the public
   interface are:
   - exposes DAG structure,
   - cloning is explicit --- no clone tracking, and
   - based on Berkeley DB transactions (as wrapped up in trails), not
     SVN transactions.  */



/* References to nodes in DAG filesystems.  */

typedef struct dag_node_t dag_node_t;


/* Open the root of revision REV of filesystem FS, as part of TRAIL.
   Set *NODE_P to the new node.  Allocate the node in TRAIL->pool.  */
svn_error_t *svn_fs__dag_revision_root (dag_node_t **node_p,
					svn_fs_t *fs,
					svn_revnum_t rev,
					trail_t *trail);


/* Open the mutable node in the transaction named TXN whose ID is ID
   in FS, as part of TRAIL; set *NODE_P to the new node.  Allocate the
   node in TRAIL->pool.  */
svn_error_t *svn_fs__dag_txn_node (dag_node_t **node_p,
				   svn_fs_t *fs,
				   const char *txn,
				   const svn_fs_id_t *id,
				   trail_t *trail);


/* Close NODE.  */
void svn_fs__dag_close (dag_node_t *node);



/* Generic node operations.  */


/* Return a new dag_node_t object referring to the same node as NODE,
   allocated in TRAIL->pool.  */
dag_node_t *svn_fs__dag_dup (dag_node_t *node,
			     trail_t *trail);


/* Return the ID of NODE.  The value returned is shared with NODE, and
   will be deallocated when NODE is.  */
const svn_fs_id_t *svn_fs__dag_get_id (dag_node_t *node);


/* Set *PROPLIST_P to a PROPLIST skel representing the entire property
   list of NODE, as part of TRAIL.  This guarantees that *PROPLIST_P
   is well-formed.  Allocate the skel in TRAIL->pool.  */
svn_error_t *svn_fs__dag_get_proplist (skel_t **proplist_p,
				       dag_node_t *node,
				       trail_t *trail);


/* Set the property list of NODE to PROPLIST, as part of TRAIL.  The
   node being changed must be mutable.  This verifies that PROPLIST is
   well-formed.  */
svn_error_t *svn_fs__dag_set_proplist (dag_node_t *node,
				       skel_t *proplist,
				       trail_t *trail);


/* Make a new mutable clone of the node named NAME in PARENT, as part
   of TRAIL.  Set *CHILD_P to a reference to the new node, allocated
   in TRAIL->pool.  PARENT must be mutable.  NAME must be a single path
   component; it cannot be a slash-separated directory path.

   This function does not record the new clone in the `clones' table.
   It's the caller's responsibility to do whatever clone tracking is
   necessary.  */
svn_error_t *svn_fs__dag_clone_child (dag_node_t **child_p,
				      dag_node_t *parent,
				      const char *name,
				      trail_t *trail);


/* Make sure the root directory of SVN_TXN in FS has been cloned as
   part of TRAIL, and set *ROOT_P to a reference to the root directory
   clone.  Allocate *ROOT_P in TRAIL->pool.  */
svn_error_t *svn_fs__dag_clone_root (dag_node_t **root_p,
				     svn_fs_t *fs,
				     const char *svn_txn,
				     trail_t *trail);


/* Commit the transaction SVN_TXN in FS, as part of TRAIL.  This entails:
   - marking the tree of mutable nodes at SVN_TXN's root as immutable,
     and marking all their contents as stable
   - creating a new revision, with SVN_TXN's root as its root directory
   - deleting SVN_TXN from `transactions'

   Beware!  This does not make sure that SVN_TXN is based on the very
   latest revision in FS.  If the caller doesn't take care of this,
   you may lose people's work!

   Do any necessary temporary allocation in a subpool of TRAIL->pool.
   Consume temporary space at most proportional to the maximum depth
   of SVN_TXN's tree of mutable nodes.  */
svn_error_t *svn_fs__dag_commit_txn (svn_fs_t *fs,
				     const char *svn_txn,
				     trail_t *trail);



/* Directories.  */


/* Open the node named NAME in the directory PARENT, as part of TRAIL.
   Set *CHILD_P to the new node, allocated in TRAIL->pool.  NAME must be a
   single path component; it cannot be a slash-separated directory
   path.  */
svn_error_t *svn_fs__dag_open (dag_node_t **child_p,
			       dag_node_t *parent,
			       const char *name,
			       trail_t *trail);


/* Create a link to CHILD in PARENT named NAME, as part of TRAIL.
   PARENT must be mutable.  NAME must be a single path component; it
   cannot be a slash-separated directory path.  */
svn_error_t *svn_fs__dag_link (dag_node_t *parent,
			       dag_node_t *child,
			       const char *name,
			       trail_t *trail);


/* Delete the directory entry named NAME from PARENT, as part of
   TRAIL.  PARENT must be mutable.  NAME must be a single path
   component; it cannot be a slash-separated directory path.  */
svn_error_t *svn_fs__dag_delete (dag_node_t *parent,
				 const char *name,
				 trail_t *trail);


/* Create a new mutable directory named NAME in PARENT, as part of
   TRAIL.  Set *CHILD_P to a reference to the new node, allocated in
   TRAIL->pool.  The new directory has no contents, and no properties.
   PARENT must be mutable.  NAME must be a single path component; it
   cannot be a slash-separated directory path.  Do any temporary
   allocation in TRAIL->pool.  */
svn_error_t *svn_fs__dag_make_dir (dag_node_t **child_p,
				   dag_node_t *parent,
				   const char *name,
				   trail_t *trail);



/* Files.  */


/* Set *CONTENTS_FN_P and *CONTENTS_BATON_P to a `read'-like function
   and baton which return the contents of FILE, as part of TRAIL.
   Allocate the BATON in TRAIL->pool.  */
svn_error_t *svn_fs__dag_get_contents (svn_read_fn_t **contents_fn_p,
				       void **contents_baton_p,
				       dag_node_t *file,
				       trail_t *trail);


/* Set the contents of FILE to CONTENTS, as part of TRAIL.  (Yes, this
   interface will need to be revised to handle large files; let's get
   things working first.)  */
svn_error_t *svn_fs__dag_set_contents (dag_node_t *file,
				       svn_string_t *contents,
				       trail_t *trail);


/* Create a new mutable file named NAME in PARENT, as part of TRAIL.
   Set *CHILD_P to a reference to the new node, allocated in
   TRAIL->pool.  The new file's contents are the empty string, and it
   has no properties.  PARENT must be mutable.  NAME must be a single
   path component; it cannot be a slash-separated directory path.  */
svn_error_t *svn_fs__dag_make_file (dag_node_t **child_p,
				    dag_node_t *parent,
				    const char *name,
				    trail_t *trail);


#endif /* SVN_LIBSVN_FS_DAG_H */
