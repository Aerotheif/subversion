/* svn_error:  common exception handling for Subversion
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 *
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 *
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 *
 * This software may consist of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 */



#include <stdarg.h>
#include "apr_strings.h"
#include "svn_error.h"



/*** helpers for creating errors ***/

static svn_error_t *
make_error_internal (apr_status_t apr_err,
                     int src_err,
                     svn_error_t *child,
                     apr_pool_t *pool,
                     const char *message,
                     va_list ap)
{
  svn_error_t *new_error;
  apr_pool_t *newpool;
  char *permanent_msg;

  /* kff todo: okay, this is where we could implement Greg Stein's
     idea.  Instead of making a new subpool of POOL, we'd discover a
     `global' error pool as an attribute of POOL, and make a subpool
     under that.  (It's important to still make a subpool, so that
     callers can free individual errors).  Before we make such a
     change, we need to ensure that all subpool creation in Subversion
     happens in an inherity way, i.e., through apr_create_pool(), not
     apr_make_sub_pool(). */

  /* Make a new subpool, or maybe use child's pool. */
  if (pool)
    {
      apr_status_t apr_stat;
      apr_stat = apr_create_pool (&newpool, pool);
      if (apr_stat) /* Can't even allocate?  Get out of town. */
        exit (1);   /* kff todo: is this a good reaction? */
    }
  else if (child)
    newpool = child->pool;
  else         /* can't happen */
    exit (1);  /* kff todo: is this a good reaction? */

  /* Create the new error structure */
  new_error = (svn_error_t *) apr_palloc (newpool, sizeof(svn_error_t));

  /* Copy the message to permanent storage. */
  if (ap)
    permanent_msg = apr_pvsprintf (newpool, message, ap);
  else
    {
      permanent_msg = apr_palloc (newpool, (strlen (message) + 1));
      strcpy (permanent_msg, message);
    }

  /* Fill 'er up. */
  new_error->apr_err = apr_err;
  new_error->src_err = src_err;
  new_error->message = permanent_msg;
  new_error->child   = child;
  new_error->pool    = newpool;

  return new_error;
}


/*
  svn_create_error() : for creating nested exception structures.

  Input:  an apr_status_t error code,
          the "original" system error code, if applicable,
          a descriptive message,
          a "child" exception,
          a pool for alloc'ing

  Returns:  a new error structure (containing the old one).

 */

svn_error_t *
svn_create_error (apr_status_t apr_err,
                  int src_err,
                  svn_error_t *child,
                  apr_pool_t *pool,
                  const char *message)
{
  return make_error_internal (apr_err, src_err, child, pool, message, NULL);
}


svn_error_t *
svn_create_errorf (apr_status_t apr_err,
                   int src_err,
                   svn_error_t *child,
                   apr_pool_t *pool,
                   const char *fmt,
                   ...)
{
  svn_error_t *err;

  va_list ap;
  va_start (ap, fmt);
  err = make_error_internal (apr_err, src_err, child, pool, fmt, ap);
  va_end (ap);

  return err;
}


/* A quick n' easy way to create a wrappered exception with your own
   message, before throwing it up the stack.  (It uses all of the
   child's fields by default.)  */

svn_error_t *
svn_quick_wrap_error (svn_error_t *child, const char *new_msg)
{
  return svn_create_error (child->apr_err,
                           child->src_err,
                           child,
                           NULL,   /* allocate directly in child's pool */
                           new_msg);
}


void
svn_free_error (svn_error_t *err)
{
  apr_destroy_pool (err->pool);
}


/* Very dumb "default" error handler: Just prints out error stack
   (recursively), and quits if the fatal flag is set.  */
void
svn_handle_error (svn_error_t *err, FILE *stream)
{
  char buf[100];

  /* Pretty-print the error */
  /* Note: we can also log errors here someday. */

  /* Is this a Subversion-specific error code? */
  if ((err->apr_err > APR_OS_START_USEERR)
      && (err->apr_err <= APR_OS_START_CANONERR))
    fprintf (stream, "\nsvn_error: #%d ", err->apr_err);

  /* Otherwise, this must be an APR error code. */
  else
    fprintf (stream, "\napr_error: #%d, src_err %d, canonical err %d : %s\n",
             err->apr_err,
             err->src_err,
             apr_canonical_error (err->apr_err),
             apr_strerror (err->apr_err, buf, sizeof(buf)));

  fprintf (stream, "  %s\n", err->message);
  fflush (stream);

  if (err->child == NULL)  /* bottom of exception stack */
    {
      return;
    }
  else
    {
      /* Recurse */
      svn_handle_error (err->child, stream);
    }
}



/* Very dumb "default" warning handler -- used by all policies, unless
   svn_svr_warning_callback() is used to set the warning handler
   differently.  */

void
svn_handle_warning (void *data, char *fmt, ...)
{
  va_list ap;

  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);

  fprintf (stderr, "\n");
  fflush (stderr);
}





/*
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
