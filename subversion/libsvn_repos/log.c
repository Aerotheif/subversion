/* log.c --- retrieving log messages
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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


#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_fs.h"
#include "svn_repos.h"
#include "svn_string.h"
#include "svn_time.h"
#include "repos.h"



/* Store as keys in CHANGED the paths of all node in ROOT that show a
 * significant change.  "Significant" means that the text or
 * properties of the node were changed, or that the node was added or
 * deleted.
 *
 * The CHANGED hash set and the key are allocated in POOL;
 * the value is (void *) 'U', 'A', 'D', or 'R', for modified, added,
 * deleted, or replaced, respectively.
 *
 */
static svn_error_t *
detect_changed (apr_hash_t **changed,
                svn_fs_root_t *root,
                apr_pool_t *pool)
{
  apr_hash_t *changes;
  apr_hash_index_t *hi;
  apr_pool_t *subpool = svn_pool_create (pool);

  *changed = apr_hash_make (pool);
  SVN_ERR (svn_fs_paths_changed (&changes, root, pool));
  for (hi = apr_hash_first (pool, changes); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      svn_fs_path_change_t *change;
      const char *path;
      char action;
      svn_log_changed_path_t *item;

      svn_pool_clear (subpool);

      /* KEY will be the path, VAL the change. */
      apr_hash_this (hi, &key, NULL, &val);
      path = (const char *) key;
      change = val;

      switch (change->change_kind)
        {
        case svn_fs_path_change_reset:
          continue;

        case svn_fs_path_change_add:
          action = 'A';
          break;

        case svn_fs_path_change_replace:
          action = 'R';
          break;

        case svn_fs_path_change_delete:
          action = 'D';
          break;

        case svn_fs_path_change_modify:
        default:
          action = 'M';
          break;
        }

      item = apr_pcalloc (pool, sizeof (*item));
      item->action = action;
      item->copyfrom_rev = SVN_INVALID_REVNUM;
      if ((action == 'A') || (action == 'R'))
        {
          const char *copyfrom_path;
          svn_revnum_t copyfrom_rev;

          SVN_ERR (svn_fs_copied_from (&copyfrom_rev, &copyfrom_path,
                                       root, path, subpool));
          if (copyfrom_path && SVN_IS_VALID_REVNUM (copyfrom_rev))
            {
              item->copyfrom_path = apr_pstrdup (pool, copyfrom_path);
              item->copyfrom_rev = copyfrom_rev;
            }
        }
      apr_hash_set (*changed, apr_pstrdup (pool, path),
                    APR_HASH_KEY_STRING, item);
    }

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_repos_get_logs (svn_repos_t *repos,
                    const apr_array_header_t *paths,
                    svn_revnum_t start,
                    svn_revnum_t end,
                    svn_boolean_t discover_changed_paths,
                    svn_boolean_t strict_node_history,
                    svn_log_message_receiver_t receiver,
                    void *receiver_baton,
                    apr_pool_t *pool)
{
  svn_revnum_t this_rev, head = SVN_INVALID_REVNUM;
  apr_pool_t *subpool = svn_pool_create (pool);
  svn_fs_t *fs = repos->fs;
  apr_array_header_t *revs = NULL;

  SVN_ERR (svn_fs_youngest_rev (&head, fs, pool));

  if (! SVN_IS_VALID_REVNUM (start))
    start = head;

  if (! SVN_IS_VALID_REVNUM (end))
    end = head;

  /* Check that revisions are sane before ever invoking receiver. */
  if (start > head)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_REVISION, 0,
       "svn_repos_get_logs: No such revision `%" SVN_REVNUM_T_FMT "'", start);
  if (end > head)
    return svn_error_createf
      (SVN_ERR_FS_NO_SUCH_REVISION, 0,
       "svn_repos_get_logs: No such revision `%" SVN_REVNUM_T_FMT "'", end);

  /* If paths were specified, then we only really care about revisions
     in which those paths were changed.  So we ask the filesystem for
     all the revisions in which any of the paths was changed.  */
  if (paths && paths->nelts)
    {
      svn_fs_root_t *rev_root;

      /* Set the revision root to the newer of the revisions we are
         searching for.  */
      SVN_ERR (svn_fs_revision_root
               (&rev_root, fs, (start > end) ? start : end, pool));

      /* And the search is on... */
      SVN_ERR (svn_fs_revisions_changed (&revs, rev_root, paths,
                                         strict_node_history ? 0 : 1, pool));

      /* If no revisions were found for these entries, we have nothing
         to show. Just return now before we break a sweat.  */
      if (! (revs && revs->nelts))
        return SVN_NO_ERROR;
    }

  for (this_rev = start;
       ((start >= end) ? (this_rev >= end) : (this_rev <= end));
       ((start >= end) ? this_rev-- : this_rev++))
    {
      svn_string_t *author, *date, *message;
      apr_hash_t *changed_paths = NULL;

      /* If we have a list of revs for use, check to make sure this is
         one of them.  */
      if (revs)
        {
          int i, matched = 0;
          for (i = 0; ((i < revs->nelts) && (! matched)); i++)
            {
              if (this_rev == ((svn_revnum_t *)(revs->elts))[i])
                matched = 1;
            }

          if (! matched)
            continue;
        }

      SVN_ERR (svn_fs_revision_prop
               (&author, fs, this_rev, SVN_PROP_REVISION_AUTHOR, subpool));
      SVN_ERR (svn_fs_revision_prop
               (&date, fs, this_rev, SVN_PROP_REVISION_DATE, subpool));
      SVN_ERR (svn_fs_revision_prop
               (&message, fs, this_rev, SVN_PROP_REVISION_LOG, subpool));

      /* ### Below, we discover changed paths if the user requested
         them (i.e., "svn log -v" means `discover_changed_paths' will
         be non-zero here).  */

      if ((this_rev > 0) && discover_changed_paths)
        {
          svn_fs_root_t *newroot;
          SVN_ERR (svn_fs_revision_root (&newroot, fs, this_rev, subpool));
          SVN_ERR (detect_changed (&changed_paths, newroot, subpool));
        }

      SVN_ERR ((*receiver) (receiver_baton,
                            changed_paths,
                            this_rev,
                            author ? author->data : NULL,
                            date ? date->data : NULL,
                            message ? message->data : NULL,
                            subpool));

      svn_pool_clear (subpool);
    }

  svn_pool_destroy (subpool);

  return SVN_NO_ERROR;
}
