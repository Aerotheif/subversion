/*
 * update-cmd.c -- Bring work tree in sync with repository
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_error.h"
#include "cl.h"


/*** Code. ***/

svn_error_t *
svn_cl__update (svn_cl__opt_state_t *opt_state,
                apr_array_header_t *targets,
                apr_pool_t *pool)
{
  svn_error_t *err;
  int i;

  if (targets->nelts)
    for (i = 0; i < targets->nelts; i++)
      {
        svn_string_t *target = ((svn_string_t **) (targets->elts))[i];
        const svn_delta_edit_fns_t *trace_editor;
        void *trace_edit_baton;

        err = svn_cl__get_trace_update_editor (&trace_editor,
                                               &trace_edit_baton,
                                               target, pool);
        if (err)
          return err;

        err = svn_client_update (NULL, NULL,
                                 trace_editor, trace_edit_baton,
                                 target,
                                 opt_state->xml_file,
                                 opt_state->revision,
                                 pool);
        if (err)
          return err;
      }
  else
    {
      fprintf (stderr, "svn update: arguments required\n");
      err = svn_cl__help (opt_state, targets, pool);
      if (err)
        return err;
    }

  return SVN_NO_ERROR;
}


/*
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
