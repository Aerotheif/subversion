/*
 * commit.c:  wrappers around wc commit functionality.
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
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 */

/* ==================================================================== */



/*** Includes. ***/

#include <assert.h>
#include <apr_strings.h>
#include <apr_uuid.h>
#include "svn_wc.h"
#include "svn_delta.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"



/*** Helpers. ***/
static svn_error_t *
generic_write (void *baton,
               const char *buffer,
               apr_size_t *len,
               apr_pool_t *pool)
{
  apr_file_t *dst = (apr_file_t *) baton;
  apr_status_t stat;

  stat = apr_full_write (dst, buffer, (apr_size_t) *len, (apr_size_t *) len);

  if (stat && (stat != APR_EOF))
    return
      svn_error_create (stat, 0, NULL, pool,
                        "error writing xml delta");
  else
    return 0;
}




/*** Public Interface. ***/

svn_error_t *
svn_client_commit (svn_string_t *path,
                   svn_string_t *xml_dst,
                   svn_vernum_t version,  /* this param is temporary */
                   apr_pool_t *pool)
{
  svn_error_t *err;
  apr_status_t apr_err;
  svn_string_t *tok;
  apr_file_t *dst = NULL; /* old habits die hard */
  svn_delta_edit_fns_t *editor;
  void *edit_baton;

  /* Step 1: make a unique ID for this commit. */
#if 0  /* APR's uuid support isn't quite done yet. */
  apr_uuid_t uuid;
  char uuid_buf[APR_UUID_FORMATTED_LENGTH + 1];
  apr_get_uuid (&uuid);
  apr_format_uuid (uuid_buf, &uuid);
  tok = svn_string_create (uuid_buf, pool);
#else
  tok = svn_string_create (apr_psprintf (pool, "%ld", apr_now()), pool);
#endif /* 0 */

  /* Step 2: look for local mods and send 'em out. */
  apr_err = apr_open (&dst, xml_dst->data,
                      (APR_WRITE | APR_CREATE),
                      APR_OS_DEFAULT,
                      pool);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "error opening %s", xml_dst->data);

  err = svn_delta_get_xml_editor (generic_write,
                                  dst,
                                  (const svn_delta_edit_fns_t **) &editor,
                                  &edit_baton,
                                  pool);
  if (err)
    return err;

  if (! path)
    path = svn_string_create (".", pool);
  err = svn_wc_crawl_local_mods (path,
                                 editor,
                                 edit_baton,
                                 tok,
                                 pool);
  if (err)
    return err;

  apr_err = apr_close (dst);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "error closing %s", xml_dst->data);

  /* Step 3: tell the working copy the commit succeeded. */
  err = svn_wc_close_commit (path, tok, version, pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}



/*
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
