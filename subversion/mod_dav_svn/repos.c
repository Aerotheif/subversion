/*
 * repos.c: mod_dav_svn repository provider functions for Subversion
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



#include <httpd.h>
#include <http_protocol.h>
#include <http_log.h>
#include <http_core.h>  /* for ap_construct_url */
#include <mod_dav.h>

#include <apr_strings.h>
#include <apr_hash.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_fs.h"

#include "dav_svn.h"


struct dav_stream {
  const dav_resource *res;
  svn_stream_t *input;
};

typedef struct {
  dav_resource res;
  dav_resource_private priv;
} dav_resource_combined;

/* private context for doing a walk */
typedef struct {
  /* the input walk parameters */
  const dav_walk_params *params;

  /* reused as we walk */
  dav_walk_resource wres;

  /* the current resource */
  dav_resource res;             /* wres.resource refers here */
  dav_resource_private info;    /* the info in res */
  svn_string_t *uri;            /* the uri within res */

} dav_svn_walker_context;


static int dav_svn_parse_version_uri(dav_resource_combined *comb,
                                     const char *path)
{
  const char *slash;

  /* format: NODE_ID/REPOS_PATH */

  comb->res.type = DAV_RESOURCE_TYPE_VERSION;
  comb->res.versioned = TRUE;

  if ((slash = ap_strchr_c(path, '/')) == NULL)
    return TRUE;

  comb->priv.node_id = svn_fs_parse_id(path, slash - path, comb->res.pool);
  if (comb->priv.node_id == NULL)
    return TRUE;

  comb->priv.repos_path = slash + 1;

  return FALSE;
}

static int dav_svn_parse_history_uri(dav_resource_combined *comb,
                                     const char *path)
{
  /* format: ??? */

  comb->res.type = DAV_RESOURCE_TYPE_HISTORY;

  /* ### parse path */
  comb->priv.repos_path = path;

  return FALSE;
}

static int dav_svn_parse_working_uri(dav_resource_combined *comb,
                                     const char *path)
{
  const char *slash;

  /* format: ACTIVITY_ID/REPOS_PATH */

  /* ### working baselines? */

  comb->res.type = DAV_RESOURCE_TYPE_WORKING;
  comb->res.working = TRUE;
  comb->res.versioned = TRUE;

  if ((slash = ap_strchr_c(path, '/')) == NULL || slash == path)
    return TRUE;

  comb->priv.root.activity_id = apr_pstrndup(comb->res.pool, path,
                                             slash - path);
  comb->priv.repos_path = slash + 1;

  return FALSE;
}

static int dav_svn_parse_activity_uri(dav_resource_combined *comb,
                                      const char *path)
{
  /* format: ACTIVITY_ID */

  comb->res.type = DAV_RESOURCE_TYPE_ACTIVITY;

  comb->priv.root.activity_id = path;

  return FALSE;
}

static const struct special_defn
{
  const char *name;

  /*
   * COMB is the resource that we are constructing. Any elements that
   * can be determined from the PATH may be set in COMB. However, further
   * operations are not allowed (we don't want anything besides a parse
   * error to occur).
   *
   * At a minimum, the parse function must set COMB->res.type and
   * COMB->priv.repos_path.
   *
   * PATH does not contain a leading slash. Given "/root/$svn/xxx/the/path"
   * as the request URI, the PATH variable will be "the/path"
   */
  int (*parse)(dav_resource_combined *comb, const char *path);

} special_subdirs[] =
{
  { "ver", dav_svn_parse_version_uri },
  { "his", dav_svn_parse_history_uri },
  { "wrk", dav_svn_parse_working_uri },
  { "act", dav_svn_parse_activity_uri },

  { NULL } /* sentinel */
};

/*
 * dav_svn_parse_uri: parse the provided URI into its various bits
 *
 * URI will contain a path relative to our configured root URI. It should
 * not have a leading "/". The root is identified by "".
 *
 * SPECIAL_URI is the component of the URI path configured by the
 * SVNSpecialPath directive (defaults to "$svn").
 *
 * On output: *COMB will contain all of the information parsed out of
 * the URI -- the resource type, activity ID, path, etc.
 *
 * Note: this function will only parse the URI. Validation of the pieces,
 * opening data stores, etc, are not part of this function.
 *
 * TRUE is returned if a parsing error occurred. FALSE for success.
 */
static int dav_svn_parse_uri(dav_resource_combined *comb,
                             const char *uri,
                             const char *special_uri)
{
  apr_size_t len1;
  apr_size_t len2;
  char ch;

  len1 = strlen(uri);
  len2 = strlen(special_uri);
  if (len1 > len2
      && ((ch = uri[len2]) == '/' || ch == '\0')
      && memcmp(uri, special_uri, len2) == 0)
    {
      if (ch == '\0')
        {
          /* URI was "/root/$svn". It exists, but has restricted usage. */
          comb->res.type = DAV_RESOURCE_TYPE_PRIVATE;
        }
      else
        {
          const struct special_defn *defn;

          /* skip past the "$svn/" prefix */
          uri += len2 + 1;
          len1 -= len2 + 1;

          for (defn = special_subdirs ; defn->name != NULL; ++defn)
            {
              apr_size_t len3 = strlen(defn->name);

              if (len1 >= len3 && memcmp(uri, defn->name, len3) == 0)
                {
                  if (uri[len3] == '\0')
                    {
                      /* URI was "/root/$svn/XXX". The location exists, but
                         has restricted usage. */
                      comb->res.type = DAV_RESOURCE_TYPE_PRIVATE;

                      /* ### store the DEFN info. we will probably want to
                         ### allow PROPFIND in "/root/$svn/act/" and will
                         ### need to know this refers to the activities
                         ### collection. */
                    }
                  else if (uri[len3] == '/')
                    {
                      if ((*defn->parse)(comb, uri + len3 + 1))
                        return TRUE;
                    }
                  else
                    {
                      /* e.g. "/root/$svn/activity" (we just know "act") */
                      return TRUE;
                    }

                  break;
                }
            }

          /* if completed the loop, then it is an unrecognized subdir */
          if (defn->name == NULL)
            return TRUE;
        }
    }
  else
    {
      /* Anything under the root, but not under "$svn". These are all
         version-controlled resources. */
      comb->res.type = DAV_RESOURCE_TYPE_REGULAR;

      /* The location of these resources corresponds directly to the URI,
         but we skip the leading "/". */
      comb->priv.repos_path = comb->priv.uri_path->data + 1;
    }

  return FALSE;
}

static dav_error * dav_svn_prep_regular(dav_resource_combined *comb)
{
  apr_pool_t *pool = comb->res.pool;
#if 0
  dav_svn_repos *repos = comb->priv.repos;
#endif
  svn_error_t *serr;

  /* ### note that we won't *always* go for the head... if this resource
     ### corresponds to a Version Resource, then we have a specific
     ### version to ask for.
  */
#if 0
  serr = svn_fs_youngest_rev(&comb->priv.root.rev, repos->fs);
#else
  /* ### svn_fs_youngest_rev() is not in the library (yet) */
  serr = NULL;
  comb->priv.root.rev = 1;
#endif
  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "Could not determine the proper "
                                 "revision to access");
    }

  /* get the root of the tree */
#if 0
  serr = svn_fs_revision_root(&comb->priv.root.root, repos->fs,
                              comb->priv.root.rev, pool);
#else
  serr = svn_error_create(0, 0, NULL, pool,
                          "svn_fs_revision_root is not implemented");
#endif
  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "Could not open the root of the "
                                 "repository");
    }

  if (*comb->priv.repos_path == '\0')
    {
      /* the root directory is always a collection */
      comb->res.collection = 1;
    }
  else
    {
      /* ### how should we test for the existence of the path? */
      serr = NULL;
      if (serr != NULL)
        {
          const char *msg;

          msg = apr_psprintf(pool, "Could not open the resource '%s'",
                             ap_escape_html(pool, comb->res.uri));
          return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR, msg);
        }

      /* is this resource a collection? */
      comb->res.collection = svn_fs_is_dir(comb->priv.root.root,
                                           comb->priv.repos_path);
    }

  /* if we are here, then the resource exists */
  comb->res.exists = TRUE;

  return NULL;
}

static dav_error * dav_svn_prep_version(dav_resource_combined *comb)
{
  /* ### look up the object, set .exists and .collection flags */
  comb->res.exists = TRUE;

  return NULL;
}

static dav_error * dav_svn_prep_history(dav_resource_combined *comb)
{
  return NULL;
}

static dav_error * dav_svn_prep_working(dav_resource_combined *comb)
{
  const char *txn_name = dav_svn_get_txn(comb->priv.repos,
                                         comb->priv.root.activity_id);

  /* ### working baselines? */

  if (txn_name == NULL)
    {
      /* ### return an error */
    }
  comb->priv.root.txn_name = txn_name;

  /* ### look up the object, set .exists and .collection flags */
  comb->res.exists = TRUE;

  /* ### if the node (path) does not exist, then see if the parent exists.
     ### this is needed for PUT, MKCOL, and COPY to create new resources.
  */

  return NULL;
}

static dav_error * dav_svn_prep_activity(dav_resource_combined *comb)
{
  const char *txn_name = dav_svn_get_txn(comb->priv.repos,
                                         comb->priv.root.activity_id);

  comb->priv.root.txn_name = txn_name;
  comb->res.exists = txn_name != NULL;

  return NULL;
}

static dav_error * dav_svn_prep_private(dav_resource_combined *comb)
{
  return NULL;
}

static const struct res_type_handler
{
  dav_resource_type type;
  dav_error * (*prep)(dav_resource_combined *comb);

} res_type_handlers[] =
{
  /* skip UNKNOWN */
  { DAV_RESOURCE_TYPE_REGULAR, dav_svn_prep_regular },
  { DAV_RESOURCE_TYPE_VERSION, dav_svn_prep_version },
  { DAV_RESOURCE_TYPE_HISTORY, dav_svn_prep_history },
  { DAV_RESOURCE_TYPE_WORKING, dav_svn_prep_working },
  /* skip WORKSPACE */
  { DAV_RESOURCE_TYPE_ACTIVITY, dav_svn_prep_activity },
  { DAV_RESOURCE_TYPE_PRIVATE, dav_svn_prep_private },

  { 0, NULL }   /* sentinel */
};

static dav_error * dav_svn_prep_resource(dav_resource_combined *comb)
{
  const struct res_type_handler *scan;

  for (scan = res_type_handlers; scan->prep != NULL; ++scan)
    {
      if (comb->res.type == scan->type)
        return (*scan->prep)(comb);
    }

  return dav_new_error(comb->res.pool, HTTP_INTERNAL_SERVER_ERROR, 0,
                       "DESIGN FAILURE: unknown resource type");
}

static dav_error * dav_svn_get_resource(request_rec *r,
                                        const char *root_path,
                                        const char *label,
                                        int use_checked_in,
                                        dav_resource **resource)
{
  const char *fs_path;
  dav_resource_combined *comb;
  dav_svn_repos *repos;
  apr_size_t len1;
  char *uri;
  const char *relative;
  svn_error_t *serr;
  dav_error *err;

  if ((fs_path = dav_svn_get_fs_path(r)) == NULL)
    {
      /* ### are SVN_ERR_APMOD codes within the right numeric space? */
      return dav_new_error(r->pool, HTTP_INTERNAL_SERVER_ERROR,
                           SVN_ERR_APMOD_MISSING_PATH_TO_FS,
                           "The server is misconfigured: an SVNPath "
                           "directive is required to specify the location "
                           "of this resource's repository.");
    }

  comb = apr_pcalloc(r->pool, sizeof(*comb));
  comb->res.info = &comb->priv;
  comb->res.hooks = &dav_svn_hooks_repos;
  comb->res.pool = r->pool;

  /* make a copy so that we can do some work on it */
  uri = apr_pstrdup(r->pool, r->uri);

  /* remove duplicate slashes */
  ap_no2slash(uri);

  /* make sure the URI does not have a trailing "/" */
  len1 = strlen(uri);
  if (len1 > 1 && uri[len1 - 1] == '/')
    uri[len1 - 1] = '\0';

  comb->res.uri = uri;

  /* The URL space defined by the SVN provider is always a virtual
     space. Construct the path relative to the configured Location
     (root_path). So... the relative location is simply the URL used,
     skipping the root_path.

     Note: mod_dav has canonialized root_path. It will not have a trailing
           slash (unless it is "/").

     Note: given a URI of /something and a root of /some, then it is
           impossible to be here (and end up with "thing"). This is simply
           because we control /some and are dispatched to here for its
           URIs. We do not control /something, so we don't get here. Or,
           if we *do* control /something, then it is for THAT root.
  */
  relative = ap_stripprefix(uri, root_path);

  /* We want a leading slash on the path specified by <relative>. This
     will almost always be the case since root_path does not have a trailing
     slash. However, if the root is "/", then the slash will be removed
     from <relative>. Backing up a character will put the leading slash
     back. */
  if (*relative != '/')
      --relative;

  /* "relative" is part of the "uri" string, so it has the proper
     lifetime to store here. */
  comb->priv.uri_path = svn_string_create(relative, r->pool);

  /* initialize this until we put something real here */
  comb->priv.root.rev = SVN_INVALID_REVNUM;

  /* create the repository structure and stash it away */
  repos = apr_pcalloc(r->pool, sizeof(*repos));
  repos->pool = r->pool;

  comb->priv.repos = repos;

  /* We are assuming the root_path will live at least as long as this
     resource. Considering that it typically comes from the per-dir
     config in mod_dav, this is valid for now. */
  repos->root_path = root_path;

  /* where is the SVN FS for this resource? */
  repos->fs_path = fs_path;

  /* Remember various bits for later URL construction */
  repos->base_url = ap_construct_url(r->pool, "", r);
  repos->special_uri = dav_svn_get_special_uri(r);

  /* open the SVN FS */
  repos->fs = svn_fs_new(r->pool);
  serr = svn_fs_open_berkeley(repos->fs, fs_path);
  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 apr_psprintf(r->pool,
                                              "Could not open the SVN "
                                              "filesystem at %s", fs_path));
    }


  /* Figure out the type of the resource */

  /* skip over the leading "/" in the relative URI */
  if (dav_svn_parse_uri(comb, relative + 1, repos->special_uri))
    goto malformed_URI;

#ifdef SVN_DEBUG
  if (comb->res.type == DAV_RESOURCE_TYPE_UNKNOWN)
    {
      DBG0("DESIGN FAILURE: should not be UNKNOWN at this point");
      goto unknown_URI;
    }
#endif

  /* prepare the resource for operation */
  if ((err = dav_svn_prep_resource(comb)) != NULL)
    return err;

  *resource = &comb->res;
  return NULL;

 malformed_URI:
  /* A malformed URI error occurs when a URI indicates the "special" area,
     yet it has an improper construction. Generally, this is because some
     doofus typed it in manually or has a buggy client. */
  /* ### are SVN_ERR_APMOD codes within the right numeric space? */
  return dav_new_error(r->pool, HTTP_INTERNAL_SERVER_ERROR,
                       SVN_ERR_APMOD_MALFORMED_URI,
                       "The URI indicated a resource within Subversion's "
                       "special resource area, but does not exist. This is "
                       "generally caused by a problem in the client "
                       "software.");

 unknown_URI:
  /* Unknown URI. Return NULL to indicate "no resource" */
  *resource = NULL;
  return NULL;
}

static dav_error * dav_svn_get_parent_resource(const dav_resource *resource,
                                               dav_resource **parent_resource)
{
  svn_string_t *path = resource->info->uri_path;

  /* the root of the repository has no parent */
  if (path->len == 1 && *path->data == '/')
    {
      *parent_resource = NULL;
      return NULL;
    }

  /* ### fill this in */
  /* ### note: only needed for methods which modify the repository */

  return NULL;
}

/* does RES2 live in the same repository as RES1? */
static int is_our_resource(const dav_resource *res1,
                           const dav_resource *res2)
{
  if (res1->hooks != res2->hooks
      || strcmp(res1->info->repos->fs_path, res2->info->repos->fs_path) != 0)
    {
      /* a different provider, or a different FS repository */
      return 0;
    }

  /* coalesce the repository */
  if (res1->info->repos != res2->info->repos)
    {
        /* ### crap. what to do with this... */
        (void) svn_fs_close_fs(res2->info->repos->fs);
        res2->info->repos = res1->info->repos;
    }

  return 1;
}

static int dav_svn_is_same_resource(const dav_resource *res1,
                                    const dav_resource *res2)
{
  if (!is_our_resource(res1, res2))
    return 0;

  /* ### what if the same resource were reached via two URIs? */

  return svn_string_compare(res1->info->uri_path, res2->info->uri_path);
}

static int dav_svn_is_parent_resource(const dav_resource *res1,
                                      const dav_resource *res2)
{
  apr_size_t len1 = strlen(res1->info->uri_path->data);
  apr_size_t len2;

  if (!is_our_resource(res1, res2))
    return 0;

  /* ### what if a resource were reached via two URIs? we ought to define
     ### parent/child relations for resources independent of URIs.
     ### i.e. define a "canonical" location for each resource, then return
     ### the parent based on that location. */

  /* res2 is one of our resources, we can use its ->info ptr */
  len2 = strlen(res2->info->uri_path->data);

  return (len2 > len1
          && memcmp(res1->info->uri_path->data, res2->info->uri_path->data,
                    len1) == 0
          && res2->info->uri_path->data[len1] == '/');
}

static dav_error * dav_svn_open_stream(const dav_resource *resource,
                                       dav_stream_mode mode,
                                       dav_stream **stream)
{
#if 0
  svn_error_t *serr;
#endif

  if (mode == DAV_MODE_WRITE_TRUNC || mode == DAV_MODE_WRITE_SEEKABLE)
    {
      if (resource->type != DAV_RESOURCE_TYPE_WORKING)
        {
          return dav_new_error(resource->pool, HTTP_METHOD_NOT_ALLOWED, 0,
                               "Resource body changes may only be made to "
                               "working resources [at this time].");
        }
    }
  else
    {
    }

  /* start building the stream structure */
  *stream = apr_pcalloc(resource->pool, sizeof(**stream));
  (*stream)->res = resource;

#if 0
  /* ### assuming mode == read for now */

  serr = svn_fs_file_contents(&(*stream)->input, resource->info->root.root,
                              resource->info->repos_path,
                              resource->pool);
  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "could not prepare to read the file");
    }
#endif

  return NULL;
}

static dav_error * dav_svn_close_stream(dav_stream *stream, int commit)
{
  /* ### anything that needs to happen with stream->baton? */

#if 0
  svn_fs_free_file_contents(stream->baton);
#endif

  return NULL;
}

static dav_error * dav_svn_read_stream(dav_stream *stream, void *buf,
                                       apr_size_t *bufsize)
{
  svn_error_t *serr;

  serr = svn_stream_read (stream->input, buf, bufsize);
  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "could not read the file contents");
    }

  return NULL;
}

static dav_error * dav_svn_write_stream(dav_stream *stream, const void *buf,
                                        apr_size_t bufsize)
{
  /* ### svn_fs_apply_textdelta */

  /* ### fill this in */
  return NULL;
}

static dav_error * dav_svn_seek_stream(dav_stream *stream,
                                       apr_off_t abs_position)
{
  /* ### fill this in */
  return NULL;
}

const char * dav_svn_getetag(const dav_resource *resource)
{
  svn_error_t *serr;
  svn_fs_id_t *id;      /* ### want const here */
  svn_string_t *idstr;

  if (!resource->exists)
    return "";

  /* ### what kind of etag to return for collections, activities, etc? */

  serr = svn_fs_node_id(&id, resource->info->root.root,
                        resource->info->repos_path, resource->pool);
  if (serr != NULL) {
    /* ### what to do? */
    return "";
  }

  idstr = svn_fs_unparse_id(id, resource->pool);
  return apr_psprintf(resource->pool, "\"%s\"", idstr);
}

static dav_error * dav_svn_set_headers(request_rec *r,
                                       const dav_resource *resource)
{
#if 0
  apr_off_t length;
#endif

  if (!resource->exists)
    return NULL;

  /* ### what to do for collections, activities, etc */

  /* make sure the proper mtime is in the request record */
#if 0
  ap_update_mtime(r, resource->info->finfo.mtime);
#endif

  /* ### note that these use r->filename rather than <resource> */
#if 0
  ap_set_last_modified(r);
#endif

  /* generate our etag and place it into the output */
  apr_table_setn(r->headers_out, "ETag", dav_svn_getetag(resource));

  /* we accept byte-ranges */
  apr_table_setn(r->headers_out, "Accept-Ranges", "bytes");

  /* set up the Content-Length header */
#if 0
  serr = svn_fs_file_length(&length, resource->info->root.root,
                            resource->info->repos_path,
                            resource->pool);
  if (serr != NULL)
    {
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "could not fetch the resource length");
    }
  ap_set_content_length(r, length);
#endif

  /* ### how to set the content type? */
  /* ### until this is resolved, the Content-Type header is busted */

  return NULL;
}

static dav_error * dav_svn_create_collection(dav_resource *resource)
{
  svn_error_t *serr;

  if (resource->type != DAV_RESOURCE_TYPE_WORKING)
    {
      return dav_new_error(resource->pool, HTTP_METHOD_NOT_ALLOWED, 0,
                           "Collections can only be created within a working "
                           "collection [at this time].");
    }

  /* ### note that the parent was checked out at some point, and this
     ### is being preformed relative to the working rsrc for that parent */

  /* ### is the root opened yet? */

  if ((serr = svn_fs_make_dir(resource->info->root.root,
                              resource->info->repos_path,
                              resource->pool)) != NULL)
    {
      /* ### need a better error */
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "Could not create the collection.");
    }

  return NULL;
}

static dav_error * dav_svn_copy_resource(const dav_resource *src,
                                         dav_resource *dst,
                                         int depth,
                                         dav_response **response)
{
  /* ### source must be from a collection under baseline control. the
     ### baseline will (implicitly) indicate the source revision, and the
     ### path will be derived simply from the URL path */

  /* ### the destination's parent must be a working collection */

  return NULL;
}

static dav_error * dav_svn_move_resource(dav_resource *src,
                                         dav_resource *dst,
                                         dav_response **response)
{
  /* NOTE: Subversion does not use the MOVE method. Strictly speaking,
     we do not need to implement this repository function. */

  /* ### fill this in */
  return NULL;
}

static dav_error * dav_svn_remove_resource(dav_resource *resource,
                                           dav_response **response)
{
  svn_error_t *serr;

  if (resource->type != DAV_RESOURCE_TYPE_WORKING)
    {
      return dav_new_error(resource->pool, HTTP_METHOD_NOT_ALLOWED, 0,
                           "Resources can only be deleted from within a "
                           "working collection [at this time].");
    }

  /* ### note that the parent was checked out at some point, and this
     ### is being preformed relative to the working rsrc for that parent */

  /* NOTE: strictly speaking, we cannot determine whether the parent was
     ever checked out, and that this working resource is relative to that
     checked out parent. It is entirely possible the client checked out
     the target resource and just deleted it. Subversion doesn't mind, but
     this does imply we are not enforcing the "checkout the parent, then
     delete from within" semantic. */

  /* ### is the root opened yet? */

  if ((serr = svn_fs_delete_tree(resource->info->root.root,
                                 resource->info->repos_path,
                                 resource->pool)) != NULL)
    {
      /* ### need a better error */
      return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                                 "Could not delete the resource.");
    }

  /* ### fill this in */
  return NULL;
}

static dav_error * dav_svn_do_walk(dav_svn_walker_context *ctx, int depth)
{
  const dav_walk_params *params = ctx->params;
  int isdir = ctx->res.collection;
  dav_error *err;
  svn_error_t *serr;
  apr_hash_index_t *hi;
  apr_size_t path_len;
  apr_size_t uri_len;
  apr_hash_t *children;

  /* The current resource is a collection (possibly here thru recursion)
     and this is the invocation for the collection. Alternatively, this is
     the first [and only] entry to do_walk() for a member resource, so
     this will be the invocation for the member. */
  err = (*params->func)(&ctx->wres,
                        isdir ? DAV_CALLTYPE_COLLECTION : DAV_CALLTYPE_MEMBER);
  if (err != NULL)
    return err;

  /* if we are not to recurse, or this is a member, then we're done */
  if (depth == 0 || !isdir)
    return NULL;

  /* assert: collection resource. isdir == TRUE */

  /* append "/" to the path, in preparation for appending child names */
  svn_string_appendcstr(ctx->info.uri_path, "/");

  /* NOTE: the URI should already have a trailing "/" */

  /* all of the children exist. also initialize the collection flag. */
  ctx->res.exists = 1;
  ctx->res.collection = 0;

  /* remember these values so we can chop back to them after each time
     we append a child name to the path/uri */
  path_len = ctx->info.uri_path->len;
  uri_len = ctx->uri->len;

  /* fetch this collection's children */
  /* ### shall we worry about filling params->pool? */
  /* ### assuming REGULAR resource. uri_path is repository path. not using
     ### repos_path because the uri_path manipulation above may have changed
     ### repos_path's intended memory location. */
  serr = svn_fs_dir_entries(&children, ctx->info.root.root,
                            ctx->info.uri_path->data + 1, params->pool);
  if (serr != NULL)
    return dav_svn_convert_err(serr, HTTP_INTERNAL_SERVER_ERROR,
                               "could not fetch collection members");

  /* iterate over the children in this collection */
  for (hi = apr_hash_first(children); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      apr_size_t klen;
      void *val;
      svn_fs_dirent_t *dirent;

      /* fetch one of the children */
      apr_hash_this(hi, &key, &klen, &val);
      dirent = val;

      /* authorize access to this resource, if applicable */
      if (params->walk_type & DAV_WALKTYPE_AUTH)
        {
          /* ### how/what to do? */
        }

      /* append this child to our buffers */
      svn_string_appendbytes(ctx->info.uri_path, key, klen);
      svn_string_appendbytes(ctx->uri, key, klen);

      /* reset the URI pointer since the above may have changed it */
      ctx->res.uri = ctx->uri->data;

      /* reset the repos_path in case the above may have changed it */
      /* ### assuming REGULAR resource. uri_path is repository path */
      ctx->info.repos_path = ctx->info.uri_path->data + 1;

      if ( svn_fs_is_file(ctx->info.root.root, ctx->info.repos_path) )
        {
          err = (*params->func)(&ctx->wres, DAV_CALLTYPE_MEMBER);
          if (err != NULL)
            return err;
        }
      else
        {
          /* this resource is a collection */
          ctx->res.collection = 1;

          /* append a slash to the URI (the path doesn't need it yet) */
          svn_string_appendcstr(ctx->uri, "/");
          ctx->res.uri = ctx->uri->data;

          /* recurse on this collection */
          err = dav_svn_do_walk(ctx, depth - 1);
          if (err != NULL)
            return err;

          /* restore the data */
          ctx->res.collection = 0;
        }

      /* chop the child off the path and uri. NOTE: no null-term. */
      ctx->info.uri_path->len = path_len;
      ctx->uri->len = uri_len;
    }

  return NULL;
}

static dav_error * dav_svn_walk(const dav_walk_params *params, int depth,
				dav_response **response)
{
  dav_svn_walker_context ctx = { 0 };
  dav_error *err;

  /* ### need to allow more walking in the future */
  if (params->root->type != DAV_RESOURCE_TYPE_REGULAR)
    {
      return dav_new_error(params->pool, HTTP_METHOD_NOT_ALLOWED, 0,
                           "Walking the resource hierarchy can only be done "
                           "on 'regular' resources [at this time].");
    }

  ctx.params = params;

  ctx.wres.walk_ctx = params->walk_ctx;
  ctx.wres.pool = params->pool;
  ctx.wres.resource = &ctx.res;

  /* copy the resource over and adjust the "info" reference */
  ctx.res = *params->root;
  ctx.info = *ctx.res.info;

  ctx.res.info = &ctx.info;

  /* operate within the proper pool */
  ctx.res.pool = params->pool;

  /* Don't monkey with the path from params->root. Create a new one.
     This path will then be extended/shortened as necessary. */
  ctx.info.uri_path = svn_string_dup(ctx.info.uri_path, params->pool);

  /* prep the URI buffer */
  ctx.uri = svn_string_create(params->root->uri, params->pool);

  /* if we have a collection, then ensure the URI has a trailing "/" */
  /* ### get_resource always kills the trailing slash... */
  if (ctx.res.collection && ctx.uri->data[ctx.uri->len - 1] != '/') {
    svn_string_appendcstr(ctx.uri, "/");
  }

  /* the current resource's URI is stored in the (telescoping) ctx.uri */
  ctx.res.uri = ctx.uri->data;

  /* the current resource's repos_path is stored in ctx.info.uri_path */
  /* ### assuming REGULAR resource. uri_path is repository path */
  ctx.info.repos_path = ctx.info.uri_path->data + 1;

  /* ### is the root already/always open? need to verify */

  /* always return the error, and any/all multistatus responses */
  err = dav_svn_do_walk(&ctx, depth);
  *response = ctx.wres.response;
  return err;
}


/*** Utility functions for resource management ***/

dav_resource *dav_svn_create_working_resource(const dav_resource *base,
                                              const char *activity_id,
                                              const char *txn_name,
                                              const char *repos_path)
{
  dav_resource_combined *comb;
  svn_string_t *path = svn_string_createf(base->pool, "/%s/wrk/%s/%s",
                                          base->info->repos->special_uri,
                                          activity_id, repos_path);


  comb = apr_pcalloc(base->pool, sizeof(*comb));

  comb->res.type = DAV_RESOURCE_TYPE_WORKING;
  comb->res.exists = 1;         /* ### not necessarily true */
  comb->res.collection = 0;     /* ### not necessarily true */
  comb->res.versioned = 1;
  comb->res.baselined = 0;      /* ### not necessarily true */
  comb->res.working = 1;
  comb->res.uri = apr_pstrcat(base->pool, base->info->repos->root_path,
                              path->data, NULL);
  comb->res.info = &comb->priv;
  comb->res.hooks = &dav_svn_hooks_repos;
  comb->res.pool = base->pool;

  comb->priv.uri_path = path;
  comb->priv.repos = base->info->repos;
  comb->priv.repos_path = repos_path;
  comb->priv.root.rev = SVN_INVALID_REVNUM;
  comb->priv.root.activity_id = activity_id;
  comb->priv.root.txn_name = txn_name;

  return &comb->res;
}


const dav_hooks_repository dav_svn_hooks_repos =
{
  1,                            /* special GET handling */
  dav_svn_get_resource,
  dav_svn_get_parent_resource,
  dav_svn_is_same_resource,
  dav_svn_is_parent_resource,
  dav_svn_open_stream,
  dav_svn_close_stream,
  dav_svn_read_stream,
  dav_svn_write_stream,
  dav_svn_seek_stream,
  dav_svn_set_headers,
  NULL,                         /* get_pathname */
  NULL,                         /* free_file */
  dav_svn_create_collection,
  dav_svn_copy_resource,
  dav_svn_move_resource,
  dav_svn_remove_resource,
  dav_svn_walk,
  dav_svn_getetag,
};


/*
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
