/* dag.c : DAG-like interface filesystem, private to libsvn_fs
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

#include "svn_fs.h"
#include "dag.h"
#include "err.h"
#include "fs.h"
#include "nodes-table.h"
#include "rev-table.h"
#include "skel.h"
#include "trail.h"



/* Initializing a filesystem.  */

/* Trail body for svn_fs__dag_init_fs. */
static svn_error_t *
dag_init_fs (void *fs_baton, trail_t *trail)
{
  svn_fs_t *fs = fs_baton;

  /* xbc FIXME: We're creating skels from constant data here. That
     means the compiler is going to complain about discarding `const',
     but I refuse to cast away the const, and I'm certainly not about
     to go building skels by hand; the code is much clearer this
     way. Maybe svn_fs__parse_skel should take a `const char*'
     parameter after all.  */

  /* Create empty root directory with node revision 0.0:
     "nodes" : "0.0" -> "(fulltext [(dir ()) ()])" */
  {
    static const char rep_skel[] = "(fulltext ((dir ()) ()))";
    SVN_ERR (svn_fs__put_rep (fs,
                              svn_fs_parse_id ("0.0", 3, trail->pool),
                              svn_fs__parse_skel (rep_skel,
                                                  sizeof (rep_skel) - 1,
                                                  trail->pool),
                              trail->db_txn,
                              trail->pool));
  }

  /* Link it into filesystem revision 0:
     "revisions" : 0 -> "(revision  3 0.0  ())" */
  {
    static const char rev_skel[] = "(revision  3 0.0  ())";
    svn_revnum_t rev = 0;
    SVN_ERR (svn_fs__put_rev (&rev, fs,
                              svn_fs__parse_skel (rev_skel,
                                                  sizeof (rev_skel) - 1,
                                                  trail->pool),
                              trail->db_txn,
                              trail->pool));

    if (rev != 0)
      return svn_error_createf (SVN_ERR_FS_CORRUPT, 0, 0, fs->pool,
                                "initial revision number is not `0'"
                                " in filesystem `%s'",
                                fs->env_path);
  }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_fs__dag_init_fs (svn_fs_t *fs)
{
  return svn_fs__retry_txn (fs, dag_init_fs, fs, fs->pool);
}



/*
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
