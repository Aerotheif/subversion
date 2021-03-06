/*
 * locks.c :  entry point for locking RA functions for ra_serf
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



#include <apr_uri.h>
#include <serf.h>

#include "svn_dav.h"
#include "svn_pools.h"
#include "svn_ra.h"

#include "../libsvn_ra/ra_loader.h"
#include "svn_config.h"
#include "svn_path.h"
#include "svn_time.h"
#include "svn_private_config.h"

#include "ra_serf.h"


/*
 * This enum represents the current state of our XML parsing for a REPORT.
 */
enum {
  INITIAL = 0,
  MULTISTATUS,
  RESPONSE,
  PROPSTAT,
  PROP,
  LOCK_DISCOVERY,
  ACTIVE_LOCK,
  LOCK_TYPE,
  LOCK_SCOPE,
  DEPTH,
  TIMEOUT,
  LOCK_TOKEN,
  OWNER,
  HREF
};

typedef struct lock_info_t {
  apr_pool_t *pool;

  const char *path;

  svn_lock_t *lock;

  svn_boolean_t force;
  svn_revnum_t revision;

  svn_boolean_t read_headers;

  svn_ra_serf__handler_t *handler;

  /* The expat handler. We wrap this to do a bit more work.  */
  svn_ra_serf__response_handler_t inner_handler;
  void *inner_baton;

} lock_info_t;

#define D_ "DAV:"
#define S_ SVN_XML_NAMESPACE
static const svn_ra_serf__xml_transition_t locks_ttable[] = {
  /* The INITIAL state can transition into D:prop (LOCK) or
     to D:multistatus (PROPFIND)  */
  { INITIAL, D_, "prop", PROP,
    FALSE, { NULL }, FALSE },
  { INITIAL, D_, "multistatus", MULTISTATUS,
    FALSE, { NULL }, FALSE },

  { MULTISTATUS, D_, "response", RESPONSE,
    FALSE, { NULL }, FALSE },

  { RESPONSE, D_, "propstat", PROPSTAT,
    FALSE, { NULL }, FALSE },

  { PROPSTAT, D_, "prop", PROP,
    FALSE, { NULL }, FALSE },

  { PROP, D_, "lockdiscovery", LOCK_DISCOVERY,
    FALSE, { NULL }, FALSE },

  { LOCK_DISCOVERY, D_, "activelock", ACTIVE_LOCK,
    FALSE, { NULL }, FALSE },

#if 0
  /* ### we don't really need to parse locktype/lockscope. we know what
     ### the values are going to be. we *could* validate that the only
     ### possible children are D:write and D:exclusive. we'd need to
     ### modify the state transition to tell us about all children
     ### (ie. maybe support "*" for the name) and then validate. but it
     ### just isn't important to validate, so disable this for now... */

  { ACTIVE_LOCK, D_, "locktype", LOCK_TYPE,
    FALSE, { NULL }, FALSE },

  { LOCK_TYPE, D_, "write", WRITE,
    FALSE, { NULL }, TRUE },

  { ACTIVE_LOCK, D_, "lockscope", LOCK_SCOPE,
    FALSE, { NULL }, FALSE },

  { LOCK_SCOPE, D_, "exclusive", EXCLUSIVE,
    FALSE, { NULL }, TRUE },
#endif /* 0  */

  { ACTIVE_LOCK, D_, "timeout", TIMEOUT,
    TRUE, { NULL }, TRUE },

  { ACTIVE_LOCK, D_, "locktoken", LOCK_TOKEN,
    FALSE, { NULL }, FALSE },

  { LOCK_TOKEN, D_, "href", HREF,
    TRUE, { NULL }, TRUE },

  { ACTIVE_LOCK, D_, "owner", OWNER,
    TRUE, { NULL }, TRUE },

  /* ACTIVE_LOCK has a D:depth child, but we can ignore that.  */

  { 0 }
};


/* Conforms to svn_ra_serf__xml_closed_t  */
static svn_error_t *
locks_closed(svn_ra_serf__xml_estate_t *xes,
             void *baton,
             int leaving_state,
             const svn_string_t *cdata,
             apr_hash_t *attrs,
             apr_pool_t *scratch_pool)
{
  lock_info_t *lock_ctx = baton;

  if (leaving_state == TIMEOUT)
    {
      if (strcmp(cdata->data, "Infinite") == 0)
        lock_ctx->lock->expiration_date = 0;
      else
        SVN_ERR(svn_time_from_cstring(&lock_ctx->lock->creation_date,
                                      cdata->data, lock_ctx->pool));
    }
  else if (leaving_state == HREF)
    {
      if (cdata->len)
        {
          char *buf = apr_pstrmemdup(lock_ctx->pool, cdata->data, cdata->len);

          apr_collapse_spaces(buf, buf);
          lock_ctx->lock->token = buf;
        }
    }
  else if (leaving_state == OWNER)
    {
      if (cdata->len)
        {
          lock_ctx->lock->comment = apr_pstrmemdup(lock_ctx->pool,
                                                   cdata->data, cdata->len);
        }
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
set_lock_headers(serf_bucket_t *headers,
                 void *baton,
                 apr_pool_t *pool)
{
  lock_info_t *lock_ctx = baton;

  if (lock_ctx->force)
    {
      serf_bucket_headers_set(headers, SVN_DAV_OPTIONS_HEADER,
                              SVN_DAV_OPTION_LOCK_STEAL);
    }

  if (SVN_IS_VALID_REVNUM(lock_ctx->revision))
    {
      serf_bucket_headers_set(headers, SVN_DAV_VERSION_NAME_HEADER,
                              apr_ltoa(pool, lock_ctx->revision));
    }

  return APR_SUCCESS;
}


/* Register an error within the session. If something is already there,
   then it will take precedence.  */
static svn_error_t *
determine_error(svn_ra_serf__handler_t *handler,
                svn_error_t *err)
{
    {
      apr_status_t errcode;

      if (handler->sline.code == 423)
        errcode = SVN_ERR_FS_PATH_ALREADY_LOCKED;
      else if (handler->sline.code == 403)
        errcode = SVN_ERR_RA_DAV_FORBIDDEN;
      else
        return err;

      /* Client-side or server-side error already. Return it.  */
      if (err != NULL)
        return err;

      /* The server did not send us a detailed human-readable error.
         Provide a generic error.  */
      err = svn_error_createf(errcode, NULL,
                              _("Lock request failed: %d %s"),
                              handler->sline.code,
                              handler->sline.reason);
    }

  return err;
}


/* Implements svn_ra_serf__response_handler_t */
static svn_error_t *
handle_lock(serf_request_t *request,
            serf_bucket_t *response,
            void *handler_baton,
            apr_pool_t *pool)
{
  lock_info_t *ctx = handler_baton;

  /* 403 (Forbidden) when a lock doesn't exist.
     423 (Locked) when a lock already exists.  */
  if (ctx->handler->sline.code == 403
      || ctx->handler->sline.code == 423)
    {
      /* Go look in the body for a server-provided error. This will
         reset flags for the core handler to Do The Right Thing. We
         won't be back to this handler again.  */
      return svn_error_trace(svn_ra_serf__expect_empty_body(
                               request, response, ctx->handler, pool));
    }

  if (ctx->read_headers == FALSE)
    {
      serf_bucket_t *headers;
      const char *val;

      headers = serf_bucket_response_get_headers(response);

      val = serf_bucket_headers_get(headers, SVN_DAV_LOCK_OWNER_HEADER);
      if (val)
        {
          ctx->lock->owner = apr_pstrdup(ctx->pool, val);
        }

      val = serf_bucket_headers_get(headers, SVN_DAV_CREATIONDATE_HEADER);
      if (val)
        {
          SVN_ERR(svn_time_from_cstring(&ctx->lock->creation_date, val,
                                        ctx->pool));
        }

      ctx->read_headers = TRUE;
    }

  return ctx->inner_handler(request, response, ctx->inner_baton, pool);
}

/* Implements svn_ra_serf__request_body_delegate_t */
static svn_error_t *
create_getlock_body(serf_bucket_t **body_bkt,
                    void *baton,
                    serf_bucket_alloc_t *alloc,
                    apr_pool_t *pool)
{
  serf_bucket_t *buckets;

  buckets = serf_bucket_aggregate_create(alloc);

  svn_ra_serf__add_xml_header_buckets(buckets, alloc);
  svn_ra_serf__add_open_tag_buckets(buckets, alloc, "propfind",
                                    "xmlns", "DAV:",
                                    NULL);
  svn_ra_serf__add_open_tag_buckets(buckets, alloc, "prop", NULL);
  svn_ra_serf__add_tag_buckets(buckets, "lockdiscovery", NULL, alloc);
  svn_ra_serf__add_close_tag_buckets(buckets, alloc, "prop");
  svn_ra_serf__add_close_tag_buckets(buckets, alloc, "propfind");

  *body_bkt = buckets;
  return SVN_NO_ERROR;
}

static svn_error_t*
setup_getlock_headers(serf_bucket_t *headers,
                      void *baton,
                      apr_pool_t *pool)
{
  serf_bucket_headers_setn(headers, "Depth", "0");

  return SVN_NO_ERROR;
}

/* Implements svn_ra_serf__request_body_delegate_t */
static svn_error_t *
create_lock_body(serf_bucket_t **body_bkt,
                 void *baton,
                 serf_bucket_alloc_t *alloc,
                 apr_pool_t *pool)
{
  lock_info_t *ctx = baton;
  serf_bucket_t *buckets;

  buckets = serf_bucket_aggregate_create(alloc);

  svn_ra_serf__add_xml_header_buckets(buckets, alloc);
  svn_ra_serf__add_open_tag_buckets(buckets, alloc, "lockinfo",
                                    "xmlns", "DAV:",
                                    NULL);

  svn_ra_serf__add_open_tag_buckets(buckets, alloc, "lockscope", NULL);
  svn_ra_serf__add_tag_buckets(buckets, "exclusive", NULL, alloc);
  svn_ra_serf__add_close_tag_buckets(buckets, alloc, "lockscope");

  svn_ra_serf__add_open_tag_buckets(buckets, alloc, "locktype", NULL);
  svn_ra_serf__add_tag_buckets(buckets, "write", NULL, alloc);
  svn_ra_serf__add_close_tag_buckets(buckets, alloc, "locktype");

  if (ctx->lock->comment)
    {
      svn_ra_serf__add_tag_buckets(buckets, "owner", ctx->lock->comment,
                                   alloc);
    }

  svn_ra_serf__add_close_tag_buckets(buckets, alloc, "lockinfo");

  *body_bkt = buckets;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__get_lock(svn_ra_session_t *ra_session,
                      svn_lock_t **lock,
                      const char *path,
                      apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  svn_ra_serf__handler_t *handler;
  svn_ra_serf__xml_context_t *xmlctx;
  lock_info_t *lock_ctx;
  const char *req_url;
  svn_error_t *err;

  req_url = svn_path_url_add_component2(session->session_url.path, path, pool);

  lock_ctx = apr_pcalloc(pool, sizeof(*lock_ctx));

  lock_ctx->pool = pool;
  lock_ctx->path = req_url;
  lock_ctx->lock = svn_lock_create(pool);
  lock_ctx->lock->path = apr_pstrdup(pool, path); /* be sure  */

  xmlctx = svn_ra_serf__xml_context_create(locks_ttable,
                                           NULL, locks_closed, NULL,
                                           lock_ctx,
                                           pool);
  handler = svn_ra_serf__create_expat_handler(xmlctx, pool);

  handler->method = "PROPFIND";
  handler->path = req_url;
  handler->body_type = "text/xml";
  handler->conn = session->conns[0];
  handler->session = session;

  handler->body_delegate = create_getlock_body;
  handler->body_delegate_baton = lock_ctx;

  handler->header_delegate = setup_getlock_headers;
  handler->header_delegate_baton = lock_ctx;

  lock_ctx->inner_handler = handler->response_handler;
  lock_ctx->inner_baton = handler->response_baton;
  handler->response_handler = handle_lock;
  handler->response_baton = lock_ctx;

  lock_ctx->handler = handler;

  err = svn_ra_serf__context_run_one(handler, pool);
  err = determine_error(handler, err);

  if (handler->sline.code == 404)
    {
      return svn_error_create(SVN_ERR_RA_ILLEGAL_URL, err,
                              _("Malformed URL for repository"));
    }
  if (err)
    {
      /* TODO Shh.  We're telling a white lie for now. */
      return svn_error_create(SVN_ERR_RA_NOT_IMPLEMENTED, err,
                              _("Server does not support locking features"));
    }

  *lock = lock_ctx->lock;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__lock(svn_ra_session_t *ra_session,
                  apr_hash_t *path_revs,
                  const char *comment,
                  svn_boolean_t force,
                  svn_ra_lock_callback_t lock_func,
                  void *lock_baton,
                  apr_pool_t *scratch_pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;

  iterpool = svn_pool_create(scratch_pool);

  /* ### TODO for issue 2263: Send all the locks over the wire at once.  This
     ### loop is just a temporary shim.
     ### an alternative, which is backwards-compat with all servers is to
     ### pipeline these requests. ie. stop using run_wait/run_one.  */

  for (hi = apr_hash_first(scratch_pool, path_revs);
       hi;
       hi = apr_hash_next(hi))
    {
      svn_ra_serf__handler_t *handler;
      svn_ra_serf__xml_context_t *xmlctx;
      const char *req_url;
      lock_info_t *lock_ctx;
      svn_error_t *err;
      svn_error_t *new_err = NULL;

      svn_pool_clear(iterpool);

      lock_ctx = apr_pcalloc(iterpool, sizeof(*lock_ctx));

      lock_ctx->pool = iterpool;
      lock_ctx->path = svn__apr_hash_index_key(hi);
      lock_ctx->revision = *((svn_revnum_t*)svn__apr_hash_index_val(hi));
      lock_ctx->lock = svn_lock_create(iterpool);
      lock_ctx->lock->path = lock_ctx->path;
      lock_ctx->lock->comment = comment;

      lock_ctx->force = force;
      req_url = svn_path_url_add_component2(session->session_url.path,
                                            lock_ctx->path, iterpool);

      xmlctx = svn_ra_serf__xml_context_create(locks_ttable,
                                               NULL, locks_closed, NULL,
                                               lock_ctx,
                                               iterpool);
      handler = svn_ra_serf__create_expat_handler(xmlctx, iterpool);

      handler->method = "LOCK";
      handler->path = req_url;
      handler->body_type = "text/xml";
      handler->conn = session->conns[0];
      handler->session = session;

      handler->header_delegate = set_lock_headers;
      handler->header_delegate_baton = lock_ctx;

      handler->body_delegate = create_lock_body;
      handler->body_delegate_baton = lock_ctx;

      lock_ctx->inner_handler = handler->response_handler;
      lock_ctx->inner_baton = handler->response_baton;
      handler->response_handler = handle_lock;
      handler->response_baton = lock_ctx;

      lock_ctx->handler = handler;

      err = svn_ra_serf__context_run_one(handler, iterpool);
      err = determine_error(handler, err);

      if (lock_func)
        new_err = lock_func(lock_baton, lock_ctx->path, TRUE, lock_ctx->lock,
                            err, iterpool);
      svn_error_clear(err);

      SVN_ERR(new_err);
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

struct unlock_context_t {
  const char *token;
  svn_boolean_t force;
};

static svn_error_t *
set_unlock_headers(serf_bucket_t *headers,
                   void *baton,
                   apr_pool_t *pool)
{
  struct unlock_context_t *ctx = baton;

  serf_bucket_headers_set(headers, "Lock-Token", ctx->token);
  if (ctx->force)
    {
      serf_bucket_headers_set(headers, SVN_DAV_OPTIONS_HEADER,
                              SVN_DAV_OPTION_LOCK_BREAK);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__unlock(svn_ra_session_t *ra_session,
                    apr_hash_t *path_tokens,
                    svn_boolean_t force,
                    svn_ra_lock_callback_t lock_func,
                    void *lock_baton,
                    apr_pool_t *scratch_pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;

  iterpool = svn_pool_create(scratch_pool);

  /* ### TODO for issue 2263: Send all the locks over the wire at once.  This
     ### loop is just a temporary shim.
     ### an alternative, which is backwards-compat with all servers is to
     ### pipeline these requests. ie. stop using run_wait/run_one.  */

  for (hi = apr_hash_first(scratch_pool, path_tokens);
       hi;
       hi = apr_hash_next(hi))
    {
      svn_ra_serf__handler_t *handler;
      const char *req_url, *path, *token;
      svn_lock_t *existing_lock = NULL;
      struct unlock_context_t unlock_ctx;
      svn_error_t *err = NULL;
      svn_error_t *new_err = NULL;


      svn_pool_clear(iterpool);

      path = svn__apr_hash_index_key(hi);
      token = svn__apr_hash_index_val(hi);

      if (force && (!token || token[0] == '\0'))
        {
          SVN_ERR(svn_ra_serf__get_lock(ra_session, &existing_lock, path,
                                        iterpool));
          token = existing_lock->token;
          if (!token)
            {
              err = svn_error_createf(SVN_ERR_RA_NOT_LOCKED, NULL,
                                      _("'%s' is not locked in the repository"),
                                      path);

              if (lock_func)
                {
                  svn_error_t *err2;
                  err2 = lock_func(lock_baton, path, FALSE, NULL, err,
                                   iterpool);
                  svn_error_clear(err);
                  err = NULL;
                  if (err2)
                    return svn_error_trace(err2);
                }
              else
                {
                  svn_error_clear(err);
                  err = NULL;
                }
              continue;
            }
        }

      unlock_ctx.force = force;
      unlock_ctx.token = apr_pstrcat(iterpool, "<", token, ">", (char *)NULL);

      req_url = svn_path_url_add_component2(session->session_url.path, path,
                                            iterpool);

      handler = apr_pcalloc(iterpool, sizeof(*handler));

      handler->handler_pool = iterpool;
      handler->method = "UNLOCK";
      handler->path = req_url;
      handler->conn = session->conns[0];
      handler->session = session;

      handler->header_delegate = set_unlock_headers;
      handler->header_delegate_baton = &unlock_ctx;

      handler->response_handler = svn_ra_serf__expect_empty_body;
      handler->response_baton = handler;

      SVN_ERR(svn_ra_serf__context_run_one(handler, iterpool));

      switch (handler->sline.code)
        {
          case 204:
            break; /* OK */
          case 403:
            /* Api users expect this specific error code to detect failures */
            err = svn_error_createf(SVN_ERR_FS_LOCK_OWNER_MISMATCH, NULL,
                                    _("Unlock request failed: %d %s"),
                                    handler->sline.code,
                                    handler->sline.reason);
            break;
          default:
            err = svn_error_createf(SVN_ERR_RA_DAV_REQUEST_FAILED, NULL,
                                    _("Unlock request failed: %d %s"),
                                    handler->sline.code,
                                    handler->sline.reason);
        }

      if (lock_func)
        new_err = lock_func(lock_baton, path, FALSE, existing_lock, err,
                            iterpool);

      svn_error_clear(err);
      SVN_ERR(new_err);
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
