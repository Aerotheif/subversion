/*
 * prompt.c -- ask the user for authentication information.
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

/* ==================================================================== */



/*** Includes. ***/

#include <apr_lib.h>

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_delta.h"
#include "svn_auth.h"
#include "svn_error.h"
#include "svn_utf.h"
#include "cl.h"




/* Set @a *result to the result of prompting the user with @a prompt.
 * Allocate @a *result in @a pool.
 *
 * If @a hide is true, then try to avoid displaying the user's input.
 */
static svn_error_t *
prompt (const char **result,
        const char *prompt,
        svn_boolean_t hide,
        apr_pool_t *pool)
{
  apr_status_t status;
  apr_file_t *fp;
  char c;
  const char *prompt_native;

  svn_stringbuf_t *strbuf = svn_stringbuf_create ("", pool);

  status = apr_file_open_stdin (&fp, pool);
  if (status)
    return svn_error_create (status, NULL, "couldn't open stdin");

  SVN_ERR (svn_utf_cstring_from_utf8 (&prompt_native, prompt, pool));

  if (! hide)
    {
      svn_boolean_t saw_first_half_of_eol = FALSE;
      fprintf (stderr, "%s", prompt_native);
      fflush (stderr);

      while (1)
        {
          status = apr_file_getc (&c, fp);
          if (status && ! APR_STATUS_IS_EOF(status))
            return svn_error_create (status, NULL, "error reading stdin");

          if (saw_first_half_of_eol)
            {
              if (c == APR_EOL_STR[1])
                break;
              else
                saw_first_half_of_eol = FALSE;
            }
          else if (c == APR_EOL_STR[0])
            {
              if (sizeof(APR_EOL_STR) == 3)
                {
                  saw_first_half_of_eol = TRUE;
                  continue;
                }
              else if (sizeof(APR_EOL_STR) == 2)
                break;
              else
                /* ### APR_EOL_STR holds more than two chars?  Who
                   ever heard of such a thing? */
                abort ();
            }

          svn_stringbuf_appendbytes (strbuf, &c, 1);
        }
    }
  else
    {
      size_t bufsize = 300;
      svn_stringbuf_ensure (strbuf, bufsize);

      status = apr_password_get (prompt_native, strbuf->data, &bufsize);
      if (status)
        return svn_error_create (status, NULL, "error from apr_password_get");
    }

  SVN_ERR (svn_utf_cstring_to_utf8 ((const char **)result, strbuf->data,
                                    NULL, pool));

  return SVN_NO_ERROR;
}



/** Prompt functions for auth providers. **/

/* Helper function for auth provider prompters: mention the
 * authentication @a realm on stderr, in a manner appropriate for
 * preceding a prompt; or if @a realm is null, then do nothing.
 */
static svn_error_t *
maybe_print_realm (const char *realm, apr_pool_t *pool)
{
  const char *realm_native;

  if (realm)
    {
      SVN_ERR (svn_utf_cstring_from_utf8 (&realm_native, realm, pool));
      fprintf (stderr, "Authentication realm: %s\n", realm_native);
      fflush (stderr);
    }

  return SVN_NO_ERROR;
}


/* This implements 'svn_auth_simple_prompt_func_t'. */
svn_error_t *
svn_cl__auth_simple_prompt (svn_auth_cred_simple_t **cred_p,
                            void *baton,
                            const char *realm,
                            const char *username,
                            apr_pool_t *pool)
{
  svn_auth_cred_simple_t *ret = apr_pcalloc (pool, sizeof (*ret));
  const char *pass_prompt;

  SVN_ERR (maybe_print_realm (realm, pool));

  if (username)
    ret->username = apr_pstrdup (pool, username);
  else
    SVN_ERR (prompt (&(ret->username), "Username: ", FALSE, pool));

  pass_prompt = apr_psprintf (pool, "Password for '%s': ", ret->username);
  SVN_ERR (prompt (&(ret->password), pass_prompt, TRUE, pool));

  *cred_p = ret;
  return SVN_NO_ERROR;
}


/* This implements 'svn_auth_username_prompt_func_t'. */
svn_error_t *
svn_cl__auth_username_prompt (svn_auth_cred_username_t **cred_p,
                              void *baton,
                              const char *realm,
                              apr_pool_t *pool)
{
  svn_auth_cred_username_t *ret = apr_pcalloc (pool, sizeof (*ret));

  SVN_ERR (maybe_print_realm (realm, pool));

  SVN_ERR (prompt (&(ret->username), "Username: ", FALSE, pool));
  *cred_p = ret;
  return SVN_NO_ERROR;
}


/* This implements 'svn_auth_ssl_server_prompt_func_t'. */
svn_error_t *
svn_cl__auth_ssl_server_prompt (svn_auth_cred_server_ssl_t **cred_p,
                                void *baton,
                                int failures_in,
                                apr_pool_t *pool)
{
  svn_boolean_t previous_output = FALSE;
  int failure;
  const char *choice;

  svn_stringbuf_t *buf = svn_stringbuf_create
    ("Error validating server certificate: ", pool);

  failure = failures_in & SVN_AUTH_SSL_UNKNOWNCA;
  if (failure)
    {
      svn_stringbuf_appendcstr (buf, "Unknown certificate issuer");
      previous_output = TRUE;
    }

  failure = failures_in & SVN_AUTH_SSL_CNMISMATCH;
  if (failure)
    {
      if (previous_output)
        {
          svn_stringbuf_appendcstr (buf, ", ");
        }
      svn_stringbuf_appendcstr (buf, "Hostname mismatch");
      previous_output = TRUE;
    }
  failure = failures_in & (SVN_AUTH_SSL_EXPIRED | SVN_AUTH_SSL_NOTYETVALID);
  if (failure)
    {
      if (previous_output)
        {
          svn_stringbuf_appendcstr (buf, ", ");
        }
      svn_stringbuf_appendcstr (buf, "Certificate expired or not yet valid");
      previous_output = TRUE;
    }

  svn_stringbuf_appendcstr (buf, ". Accept? (y/N): ");
  SVN_ERR (prompt (&choice, buf->data, FALSE, pool));

  if (choice && (choice[0] == 'y' || choice[0] == 'Y'))
    {
      *cred_p = apr_pcalloc (pool, sizeof (**cred_p));
      (*cred_p)->failures_allow = failures_in;
    }
  else
    {
      *cred_p = NULL;
    }

  return SVN_NO_ERROR;
}


/* This implements 'svn_auth_ssl_client_prompt_func_t'. */
svn_error_t *
svn_cl__auth_ssl_client_prompt (svn_auth_cred_client_ssl_t **cred_p,
                                void *baton,
                                apr_pool_t *pool)
{
  const char *cert_file = NULL, *key_file = NULL;
  size_t cert_file_len;
  const char *extension;
  svn_auth_ssl_cert_type_t cert_type;

  SVN_ERR (prompt (&cert_file, "client certificate filename: ", FALSE, pool));

  if ((cert_file == NULL) || (cert_file[0] == 0))
    {
      *cred_p = NULL;
      return SVN_NO_ERROR;
    }

  cert_file_len = strlen (cert_file);
  extension = cert_file + cert_file_len - 4;
  if ((strcmp (extension, ".p12") == 0) ||
      (strcmp (extension, ".P12") == 0))
    {
      cert_type = svn_auth_ssl_pkcs12_cert_type;
    }
  else if ((strcmp (extension, ".pem") == 0) ||
           (strcmp (extension, ".PEM") == 0))
    {
      cert_type = svn_auth_ssl_pem_cert_type;
    }
  else
    {
      const char *type;
      SVN_ERR (prompt (&type, "cert type ('pem' or 'pkcs12'): ", FALSE, pool));
      if ((strcmp (type, "pkcs12") == 0) ||
          (strcmp (type, "PKCS12") == 0))
        {
          cert_type = svn_auth_ssl_pkcs12_cert_type;
        }
      else if ((strcmp (type, "pem") == 0) ||
               (strcmp (type, "PEM") == 0))
        {
          cert_type = svn_auth_ssl_pem_cert_type;
        }
      else
        {
          return svn_error_createf (SVN_ERR_INCORRECT_PARAMS, NULL,
                                    "unknown ssl certificate type '%s'", type);
        }
    }

  if (cert_type == svn_auth_ssl_pem_cert_type)
    {
      SVN_ERR (prompt (&key_file, "optional key file: ", FALSE, pool));
    }

  if (key_file && key_file[0] == 0)
    {
      key_file = NULL;
    }

  /* Build and return the credentials. */
  *cred_p = apr_pcalloc (pool, sizeof (**cred_p));
  (*cred_p)->cert_file = cert_file;
  (*cred_p)->key_file = key_file;
  (*cred_p)->cert_type = cert_type;

  return SVN_NO_ERROR;
}


/* This implements 'svn_auth_ssl_pw_prompt_func_t'. */
svn_error_t *
svn_cl__auth_ssl_pw_prompt (svn_auth_cred_client_ssl_pass_t **cred_p,
                            void *baton,
                            apr_pool_t *pool)
{

  const char *result;

  SVN_ERR (prompt (&result, "client certificate passphrase: ", TRUE, pool));

  if (result && result[0])
    {
      svn_auth_cred_client_ssl_pass_t *ret = apr_pcalloc (pool, sizeof (*ret));
      ret->password = result;
      *cred_p = ret;
    }
  else
    {
      *cred_p = NULL;
    }

  return SVN_NO_ERROR;
}



/** Generic prompting. **/

svn_error_t *
svn_cl__prompt_user (const char **result,
                     const char *prompt_str,
                     apr_pool_t *pool)
{
  return prompt (result, prompt_str, FALSE /* don't hide input */, pool);
}
