/*
 * propset-cmd.c -- Display status information in current directory
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "svn_utf.h"
#include "cl.h"


/*** Code. ***/

/* This implements the `svn_opt_subcommand_t' interface. */
svn_error_t *
svn_cl__propset (apr_getopt_t *os,
                 void *baton,
                 apr_pool_t *pool)
{
  svn_cl__opt_state_t *opt_state = baton;
  const char *pname, *pname_utf8;
  const svn_string_t *propval = NULL;
  apr_array_header_t *args, *targets;
  int i;

  /* PNAME and PROPVAL expected as first 2 arguments if filedata was
     NULL, else PNAME alone will precede the targets.  Get a UTF-8
     version of the name, too. */
  SVN_ERR (svn_opt_parse_num_args (&args, os,
                                   opt_state->filedata ? 1 : 2, pool));
  pname = ((const char **) (args->elts))[0];
  SVN_ERR (svn_utf_cstring_to_utf8 (&pname_utf8, pname, NULL, pool));

  /* Get the PROPVAL from either an external file, or from the command
     line. */
  if (opt_state->filedata)
    propval = svn_string_create_from_buf (opt_state->filedata, pool);
  else
    propval = svn_string_create (((const char **) (args->elts))[1], pool);

  /* We only want special Subversion properties to be in UTF-8.  All
     others should remain in binary format.  ### todo: make this
     happen. */
  if (svn_prop_is_svn_prop (pname_utf8))
    SVN_ERR (svn_utf_string_to_utf8 (&propval, propval, pool));

  /* Suck up all the remaining arguments into a targets array */
  SVN_ERR (svn_opt_args_to_target_array (&targets, os,
                                         opt_state->targets,
                                         &(opt_state->start_revision),
                                         &(opt_state->end_revision),
                                         FALSE, pool));

  /* Add "." if user passed 0 file arguments */
  svn_opt_push_implicit_dot_target (targets, pool);

  /* Decide if we're making a local mod to a versioned working copy
     prop, or making a permanent change to an unversioned repository
     revision prop.  The existence of the '-r' flag is the key. */
  if (opt_state->start_revision.kind != svn_opt_revision_unspecified)
    {
      svn_revnum_t rev;
      const char *URL, *target;
      svn_boolean_t is_url;
      svn_client_auth_baton_t *auth_baton;

      auth_baton = svn_cl__make_auth_baton (opt_state, pool);

      /* Either we have a URL target, or an implicit wc-path ('.')
         which needs to be converted to a URL. */
      if (targets->nelts <= 0)
        return svn_error_create(SVN_ERR_CL_INSUFFICIENT_ARGS, 0, NULL, pool,
                                "No URL target available.");
      target = ((const char **) (targets->elts))[0];
      is_url = svn_path_is_url (target);
      if (is_url)
        {
          URL = target;
        }
      else
        {
          svn_wc_adm_access_t *adm_access;
          const svn_wc_entry_t *entry;
          SVN_ERR (svn_wc_adm_probe_open (&adm_access, NULL, target,
                                          FALSE, FALSE, pool));
          SVN_ERR (svn_wc_entry (&entry, target, adm_access, FALSE, pool));
          SVN_ERR (svn_wc_adm_close (adm_access));
          URL = entry->url;
        }

      /* Let libsvn_client do the real work. */
      SVN_ERR (svn_client_revprop_set (pname_utf8, propval,
                                       URL, &(opt_state->start_revision),
                                       auth_baton, &rev, pool));
      if (! opt_state->quiet)
        {
          const char *target_native;
          SVN_ERR (svn_utf_cstring_from_utf8 (&target_native,
                                              target, pool));
          printf ("property `%s' set on repository revision '%"
                  SVN_REVNUM_T_FMT"'\n",
                  pname, rev);
        }
    }

  else
    {
      for (i = 0; i < targets->nelts; i++)
        {
          const char *target = ((const char **) (targets->elts))[i];
          SVN_ERR (svn_client_propset (pname_utf8, propval, target,
                                       opt_state->recursive, pool));

          if (! opt_state->quiet)
            {
              const char *target_native;
              SVN_ERR (svn_utf_cstring_from_utf8 (&target_native,
                                                  target, pool));
              printf ("property `%s' set%s on '%s'\n",
                      pname,
                      opt_state->recursive ? " (recursively)" : "",
                      target_native);
            }
        }
    }

  return SVN_NO_ERROR;
}



/*
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end:
 */
