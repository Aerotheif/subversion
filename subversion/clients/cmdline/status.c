/*
 * status.c:  the command-line's portion of the "svn status" command
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
#include "svn_cmdline.h"
#include "svn_wc.h"
#include "svn_path.h"
#include "cl.h"


/* Return the single character representation of STATUS */
static char
generate_status_code (enum svn_wc_status_kind status)
{
  switch (status)
    {
    case svn_wc_status_none:        return ' ';
    case svn_wc_status_normal:      return ' ';
    case svn_wc_status_added:       return 'A';
    case svn_wc_status_missing:     return '!';
    case svn_wc_status_incomplete:  return '!';
    case svn_wc_status_deleted:     return 'D';
    case svn_wc_status_replaced:    return 'R';
    case svn_wc_status_modified:    return 'M';
    case svn_wc_status_merged:      return 'G';
    case svn_wc_status_conflicted:  return 'C';
    case svn_wc_status_obstructed:  return '~';
    case svn_wc_status_ignored:     return 'I';
    case svn_wc_status_external:    return 'X';
    case svn_wc_status_unversioned: return '?';
    default:                        return '?';
    }
}

/* Print STATUS and PATH in a format determined by DETAILED and
   SHOW_LAST_COMMITTED. */
static svn_error_t *
print_status (const char *path,
              svn_boolean_t detailed,
              svn_boolean_t show_last_committed,
              svn_boolean_t repos_locks,
              svn_wc_status2_t *status,
              apr_pool_t *pool)
{
  if (detailed)
    {
      char ood_status, lock_status;
      const char *working_rev;

      if (! status->entry)
        working_rev = "";
      else if (! SVN_IS_VALID_REVNUM (status->entry->revision))
        working_rev = " ? ";
      else if (status->copied)
        working_rev = "-";
      else
        working_rev = apr_psprintf (pool, "%ld", status->entry->revision);

      if (status->repos_text_status != svn_wc_status_none
          || status->repos_prop_status != svn_wc_status_none)
        ood_status = '*';
      else
        ood_status = ' ';

      if (repos_locks)
        {
          if (status->repos_lock)
            {
              if (status->entry && status->entry->lock_token)
                {
                  if (strcmp (status->repos_lock->token, status->entry->lock_token)
                      == 0)
                    lock_status = 'K';
                  else
                    lock_status = 'T';
                }
              else
                lock_status = 'O';
            }
          else if (status->entry && status->entry->lock_token)
            lock_status = 'B';
          else
            lock_status = ' ';
        }
      else
        lock_status = (status->entry && status->entry->lock_token) ? 'K' : ' ';

      if (show_last_committed)
        {
          const char *commit_rev;
          const char *commit_author;

          if (status->entry && SVN_IS_VALID_REVNUM (status->entry->cmt_rev))
            commit_rev = apr_psprintf(pool, "%ld", status->entry->cmt_rev);
          else if (status->entry)
            commit_rev = " ? ";
          else
            commit_rev = "";

          if (status->entry && status->entry->cmt_author)
            commit_author = status->entry->cmt_author;
          else if (status->entry)
            commit_author = " ? ";
          else
            commit_author = "";

          SVN_ERR
            (svn_cmdline_printf (pool,
                                 "%c%c%c%c%c%c %c   %6s   %6s %-12s %s\n",
                                 generate_status_code (status->text_status),
                                 generate_status_code (status->prop_status),
                                 status->locked ? 'L' : ' ',
                                 status->copied ? '+' : ' ',
                                 status->switched ? 'S' : ' ',
                                 lock_status,
                                 ood_status,
                                 working_rev,
                                 commit_rev,
                                 commit_author,
                                 path));
        }
      else
        SVN_ERR
          (svn_cmdline_printf (pool, "%c%c%c%c%c%c %c   %6s   %s\n",
                               generate_status_code (status->text_status),
                               generate_status_code (status->prop_status),
                               status->locked ? 'L' : ' ',
                               status->copied ? '+' : ' ',
                               status->switched ? 'S' : ' ',
                               lock_status,
                               ood_status,
                               working_rev,
                               path));
    }
  else
    SVN_ERR
      (svn_cmdline_printf (pool, "%c%c%c%c%c%c %s\n",
                           generate_status_code (status->text_status),
                           generate_status_code (status->prop_status),
                           status->locked ? 'L' : ' ',
                           status->copied ? '+' : ' ',
                           status->switched ? 'S' : ' ',
                           ((status->entry && status->entry->lock_token)
                            ? 'K' : ' '),
                           path));

  return SVN_NO_ERROR;
}

/* Called by status-cmd.c */
svn_error_t *
svn_cl__print_status (const char *path,
                      svn_wc_status2_t *status,
                      svn_boolean_t detailed,
                      svn_boolean_t show_last_committed,
                      svn_boolean_t skip_unrecognized,
                      svn_boolean_t repos_locks,
                      apr_pool_t *pool)
{
  if (! status
      || (skip_unrecognized && ! status->entry)
      || (status->text_status == svn_wc_status_none
          && status->repos_text_status == svn_wc_status_none))
    return SVN_NO_ERROR;

  return print_status (svn_path_local_style (path, pool),
                       detailed, show_last_committed, repos_locks, status,
                       pool);
}
