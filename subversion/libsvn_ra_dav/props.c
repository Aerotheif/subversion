/*
 * props.c :  routines for fetching DAV properties
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



#include <apr_pools.h>
#include <apr_tables.h>
#include <apr_strings.h>
#define APR_WANT_STRFUNC
#include <apr_want.h>

#include <ne_socket.h>
#include <ne_basic.h>
#include <ne_props.h>
#include <ne_xml.h>

#include "svn_error.h"
#include "svn_delta.h"
#include "svn_ra.h"
#include "svn_path.h"
#include "svn_dav.h"
#include "svn_base64.h"
#include "svn_xml.h"
#include "svn_pools.h"

#include "ra_dav.h"


/* some definitions of various properties that may be fetched */
const ne_propname svn_ra_dav__vcc_prop = {
  "DAV:", "version-controlled-configuration"
};
const ne_propname svn_ra_dav__checked_in_prop = {
  "DAV:", "checked-in"
};

/* when we begin a checkout, we fetch these from the "public" resources to
   steer us towards a Baseline Collection. we fetch the resourcetype to
   verify that we're accessing a collection. */
static const ne_propname starting_props[] =
{
  { "DAV:", "version-controlled-configuration" },
  { "DAV:", "resourcetype" },
  { SVN_DAV_PROP_NS_DAV, "baseline-relative-path" },
  { SVN_DAV_PROP_NS_DAV, "repository-uuid"},
  { NULL }
};

/* when speaking to a Baseline to reach the Baseline Collection, fetch these
   properties. */
static const ne_propname baseline_props[] =
{
  { "DAV:", "baseline-collection" },
  { "DAV:", "version-name" },
  { NULL }
};



/*** Propfind Implementation ***/

typedef struct {
  svn_ra_dav__xml_elmid id;
  const char *name;
  int is_property;      /* is it a property, or part of some structure? */
} elem_defn;


static const elem_defn elem_definitions[] =
{
  /*** NOTE: Make sure that every item in here is also represented in
       propfind_elements[] ***/

  /* DAV elements */
  { ELEM_multistatus, "DAV:multistatus", 0 },
  { ELEM_response, "DAV:response", 0 },
  { ELEM_href, "DAV:href", SVN_RA_DAV__XML_CDATA },
  { ELEM_propstat, "DAV:propstat", 0 },
  { ELEM_prop, "DAV:prop", 0 },
  { ELEM_status, "DAV:status", SVN_RA_DAV__XML_CDATA },
  { ELEM_baseline, "DAV:baseline", SVN_RA_DAV__XML_CDATA },
  { ELEM_collection, "DAV:collection", SVN_RA_DAV__XML_CDATA },
  { ELEM_resourcetype, "DAV:resourcetype", 0 },
  { ELEM_baseline_coll, SVN_RA_DAV__PROP_BASELINE_COLLECTION, 0 },
  { ELEM_checked_in, SVN_RA_DAV__PROP_CHECKED_IN, 0 },
  { ELEM_vcc, SVN_RA_DAV__PROP_VCC, 0 },
  { ELEM_version_name, SVN_RA_DAV__PROP_VERSION_NAME, 1 },
  { ELEM_get_content_length, SVN_RA_DAV__PROP_GETCONTENTLENGTH, 1 },
  { ELEM_creationdate, SVN_RA_DAV__PROP_CREATIONDATE, 1 },
  { ELEM_creator_displayname, SVN_RA_DAV__PROP_CREATOR_DISPLAYNAME, 1 },

  /* SVN elements */
  { ELEM_baseline_relpath, SVN_RA_DAV__PROP_BASELINE_RELPATH, 1 },
  { ELEM_md5_checksum, SVN_RA_DAV__PROP_MD5_CHECKSUM, 1 },
  { ELEM_repository_uuid, SVN_RA_DAV__PROP_REPOSITORY_UUID, 1 },
  { 0 }
};


static const svn_ra_dav__xml_elm_t propfind_elements[] =
{
  /*** NOTE: Make sure that every item in here is also represented in
       elem_definitions[] ***/

  /* DAV elements */
  { "DAV:", "multistatus", ELEM_multistatus, 0 },
  { "DAV:", "response", ELEM_response, 0 },
  { "DAV:", "href", ELEM_href, SVN_RA_DAV__XML_CDATA },
  { "DAV:", "propstat", ELEM_propstat, 0 },
  { "DAV:", "prop", ELEM_prop, 0 },
  { "DAV:", "status", ELEM_status, SVN_RA_DAV__XML_CDATA },
  { "DAV:", "baseline", ELEM_baseline, SVN_RA_DAV__XML_CDATA },
  { "DAV:", "baseline-collection", ELEM_baseline_coll, SVN_RA_DAV__XML_CDATA },
  { "DAV:", "checked-in", ELEM_checked_in, 0 },
  { "DAV:", "collection", ELEM_collection, SVN_RA_DAV__XML_CDATA },
  { "DAV:", "resourcetype", ELEM_resourcetype, 0 },
  { "DAV:", "version-controlled-configuration", ELEM_vcc, 0 },
  { "DAV:", "version-name", ELEM_version_name, SVN_RA_DAV__XML_CDATA },
  { "DAV:", "getcontentlength", ELEM_get_content_length,
    SVN_RA_DAV__XML_CDATA },
  { "DAV:", "creationdate", ELEM_creationdate, SVN_RA_DAV__XML_CDATA },
  { "DAV:", "creator-displayname", ELEM_creator_displayname,
    SVN_RA_DAV__XML_CDATA },

  /* SVN elements */
  { SVN_DAV_PROP_NS_DAV, "baseline-relative-path", ELEM_baseline_relpath,
    SVN_RA_DAV__XML_CDATA },
  { SVN_DAV_PROP_NS_DAV, "md5-checksum", ELEM_md5_checksum,
    SVN_RA_DAV__XML_CDATA },
  { SVN_DAV_PROP_NS_DAV, "repository-uuid", ELEM_repository_uuid,
    SVN_RA_DAV__XML_CDATA },

  /* Unknowns */
  { "", "", ELEM_unknown, SVN_RA_DAV__XML_COLLECT },

  { NULL }
};


typedef struct propfind_ctx_t
{
  apr_hash_t *props; /* const char *URL-PATH -> svn_ra_dav_resource_t */

  svn_ra_dav_resource_t *rsrc; /* the current resource. */
  const char *encoding; /* property encoding (or NULL) */
  int status; /* status for the current <propstat> (or 0 if unknown). */
  apr_hash_t *propbuffer; /* holds properties until their status is known. */
  svn_ra_dav__xml_elmid last_open_id; /* the id of the last opened tag. */
  ne_xml_parser *parser; /* xml parser handling the PROPSET request. */

  apr_pool_t *pool;

} propfind_ctx_t;


/* Look up an element definition ID.  May return NULL if the elem is
   not recognized. */
static const elem_defn *defn_from_id(svn_ra_dav__xml_elmid id)
{
  const elem_defn *defn;

  for (defn = elem_definitions; defn->name != NULL; ++defn)
    {
      if (id == defn->id)
        return defn;
    }

  return NULL;
}


/* Assign URL to RSRC.  Use POOL for any allocations. */
static void assign_rsrc_url(svn_ra_dav_resource_t *rsrc,
                            const char *url,
                            apr_pool_t *pool)
{
  char *url_path;
  apr_size_t len;
  ne_uri parsed_url;

  /* Parse the PATH element out of the URL.
     NOTE: mod_dav does not (currently) use an absolute URL, but simply a
     server-relative path (i.e. this uri_parse is effectively a no-op).
  */
  (void) ne_uri_parse(url, &parsed_url);
  url_path = apr_pstrdup(pool, parsed_url.path);
  ne_uri_free(&parsed_url);

  /* Clean up trailing slashes from the URL. */
  len = strlen(url_path);
  if (len > 1 && url_path[len - 1] == '/')
    url_path[len - 1] = '\0';
  rsrc->url = url_path;
}


static int validate_element(void *userdata,
                            svn_ra_dav__xml_elmid parent,
                            svn_ra_dav__xml_elmid child)
{
  switch (parent)
    {
    case ELEM_root:
      if (child == ELEM_multistatus)
        return SVN_RA_DAV__XML_VALID;
      else
        return SVN_RA_DAV__XML_INVALID;

    case ELEM_multistatus:
      if (child == ELEM_response)
        return SVN_RA_DAV__XML_VALID;
      else
        return SVN_RA_DAV__XML_DECLINE;

    case ELEM_response:
      if ((child == ELEM_href) || (child == ELEM_propstat))
        return SVN_RA_DAV__XML_VALID;
      else
        return SVN_RA_DAV__XML_DECLINE;

    case ELEM_propstat:
      if ((child == ELEM_prop) || (child == ELEM_status))
        return SVN_RA_DAV__XML_VALID;
      else
        return SVN_RA_DAV__XML_DECLINE;

    case ELEM_prop:
      return SVN_RA_DAV__XML_VALID; /* handle all children of <prop> */

    case ELEM_baseline_coll:
    case ELEM_checked_in:
    case ELEM_vcc:
      if (child == ELEM_href)
        return SVN_RA_DAV__XML_VALID;
      else
        return SVN_RA_DAV__XML_DECLINE; /* not concerned with other types */

    case ELEM_resourcetype:
      if ((child == ELEM_collection) || (child == ELEM_baseline))
        return SVN_RA_DAV__XML_VALID;
      else
        return SVN_RA_DAV__XML_DECLINE; /* not concerned with other types
                                           (### now) */

    default:
      return SVN_RA_DAV__XML_DECLINE;
    }

  /* NOTREACHED */
}


static int start_element(void *userdata,
                         const svn_ra_dav__xml_elm_t *elm,
                         const char **atts)
{
  propfind_ctx_t *pc = userdata;

  switch (elm->id)
    {
    case ELEM_response:
      if (pc->rsrc)
        return SVN_RA_DAV__XML_INVALID;
      /* Create a new resource. */
      pc->rsrc = apr_pcalloc(pc->pool, sizeof(*(pc->rsrc)));
      pc->rsrc->pool = pc->pool;
      pc->rsrc->propset = apr_hash_make(pc->pool);
      pc->status = 0;
      break;

    case ELEM_propstat:
      pc->status = 0;
      break;

    case ELEM_href:
      /* Remember this <href>'s parent so that when we close this tag,
         we know to whom the URL assignment belongs.  Could be the
         resource itself, or one of the properties:
         ELEM_baseline_coll, ELEM_checked_in, ELEM_vcc: */
      pc->rsrc->href_parent = pc->last_open_id;
      break;

    case ELEM_collection:
      pc->rsrc->is_collection = 1;
      break;

    case ELEM_unknown:
      /* these are our user-visible properties, presumably. */
      pc->encoding = ne_xml_get_attr(pc->parser, atts, SVN_DAV_PROP_NS_DAV,
                                     "encoding");
      if (pc->encoding)
        pc->encoding = apr_pstrdup(pc->pool, pc->encoding);
      break;

    default:
      /* nothing to do for these */
      break;
    }

  /* Remember the last tag we opened. */
  pc->last_open_id = elm->id;
  return SVN_RA_DAV__XML_VALID;
}


static int end_element(void *userdata,
                       const svn_ra_dav__xml_elm_t *elm,
                       const char *cdata)
{
  propfind_ctx_t *pc = userdata;
  svn_ra_dav_resource_t *rsrc = pc->rsrc;
  const char *name;
  const svn_string_t *value = NULL;
  const elem_defn *parent_defn;
  const elem_defn *defn;
  ne_status status;

  switch (elm->id)
    {
    case ELEM_response:
      /* Verify that we've received a URL for this resource. */
      if (!pc->rsrc->url)
        return SVN_RA_DAV__XML_INVALID;

      /* Store the resource in the top-level hash table. */
      apr_hash_set(pc->props, pc->rsrc->url, APR_HASH_KEY_STRING, pc->rsrc);
      pc->rsrc = NULL;
      return SVN_RA_DAV__XML_VALID;

    case ELEM_propstat:
      /* We're at the end of a set of properties.  Do the right thing
         status-wise. */
      if (pc->status)
        {
          /* We have a status.  Loop over the buffered properties, and
             if the status is a good one (200), copy them into the
             resources's property hash.  Regardless of the status,
             we'll be removing these from the temporary buffer as we
             go along. */
          apr_hash_index_t *hi = apr_hash_first(pc->pool, pc->propbuffer);
          for (; hi; hi = apr_hash_next(hi))
            {
              const void *key;
              apr_ssize_t klen;
              void *val;
              apr_hash_this(hi, &key, &klen, &val);
              if (pc->status == 200)
                apr_hash_set(rsrc->propset, key, klen, val);
              apr_hash_set(pc->propbuffer, key, klen, NULL);
            }
        }
      else if (! pc->status)
        {
          /* No status at all?  Bogosity. */
          return SVN_RA_DAV__XML_INVALID;
        }
      return SVN_RA_DAV__XML_VALID;

    case ELEM_status:
      /* Parse the <status> tag's CDATA for a status code. */
      if (ne_parse_statusline(cdata, &status))
        return SVN_RA_DAV__XML_INVALID;
      free(status.reason_phrase);
      pc->status = status.code;
      return SVN_RA_DAV__XML_VALID;

    case ELEM_href:
      /* Special handling for <href> that belongs to the <response> tag. */
      if (rsrc->href_parent == ELEM_response)
        {
          assign_rsrc_url(pc->rsrc, cdata, pc->pool);
          return SVN_RA_DAV__XML_VALID;
        }

      /* Use the parent element's name, not the href. */
      parent_defn = defn_from_id(rsrc->href_parent);

      /* No known parent?  Get outta here. */
      if (!parent_defn)
        return SVN_RA_DAV__XML_VALID;

      /* All other href's we'll treat as property values. */
      name = parent_defn->name;
      value = svn_string_create(cdata, pc->pool);
      break;

    default:
      /*** This case is, as usual, for everything not covered by other
           cases.  ELM->id should be either ELEM_unknown, or one of
           the ids in the elem_definitions[] structure.  In this case,
           we seek to handle properties.  Since ELEM_unknown should
           only occur for properties, we will handle that id.  All
           other ids will be searched for in the elem_definitions[]
           structure to determine if they are properties.  Properties,
           we handle; all else hits the road.  ***/

      if (elm->id == ELEM_unknown)
        {
          name = apr_pstrcat(pc->pool, elm->nspace, elm->name, NULL);
        }
      else
        {
          defn = defn_from_id(elm->id);
          if (! (defn && defn->is_property))
            return SVN_RA_DAV__XML_VALID;
          name = defn->name;
        }

      /* Check for encoding attribute. */
      if (pc->encoding == NULL) {
        /* Handle the property value by converting it to string. */
        value = svn_string_create(cdata, pc->pool);
        break;
      }

      /* Check for known encoding type */
      if (strcmp(pc->encoding, "base64") != 0)
        return SVN_RA_DAV__XML_INVALID;

      /* There is an encoding on this property, handle it.
       * the braces are needed to allocate "in" on the stack. */
      {
        svn_string_t in;
        in.data = cdata;
        in.len = strlen(cdata);
        value = svn_base64_decode_string(&in, pc->pool);
      }

      pc->encoding = NULL; /* Reset encoding for future attribute(s). */
    }

  /*** Handling resource properties from here out. ***/

  /* Add properties to the temporary propbuffer.  At the end of the
     <propstat>, we'll either dump the props as invalid or move them
     into the resource's property hash. */
  apr_hash_set(pc->propbuffer, name, APR_HASH_KEY_STRING, value);
  return SVN_RA_DAV__XML_VALID;
}


static void set_parser(ne_xml_parser *parser,
                       void *baton)
{
  propfind_ctx_t *pc = baton;
  pc->parser = parser;
}


svn_error_t * svn_ra_dav__get_props(apr_hash_t **results,
                                    ne_session *sess,
                                    const char *url,
                                    int depth,
                                    const char *label,
                                    const ne_propname *which_props,
                                    apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  propfind_ctx_t pc;
  ne_buffer *body;
  apr_hash_t *extra_headers = apr_hash_make(pool);

  /* Add a Depth header. */
  if (depth == NE_DEPTH_ZERO)
    apr_hash_set(extra_headers, "Depth", 5, "0");
  else if (depth == NE_DEPTH_ONE)
    apr_hash_set(extra_headers, "Depth", 5, "1");
  else if (depth == NE_DEPTH_INFINITE)
    apr_hash_set(extra_headers, "Depth", 5, "infinite");
  else
    abort(); /* somebody passed some poo to our function. */

  /* If we have a label, use it. */
  if (label != NULL)
    apr_hash_set(extra_headers, "Label", 5, label);

  /* It's easier to roll our own PROPFIND here than use neon's current
     interfaces. */
  body = ne_buffer_create();

  /* The start of the request body is fixed: */
  ne_buffer_zappend(body,
                   "<?xml version=\"1.0\" encoding=\"utf-8\"?>" DEBUG_CR
                   "<propfind xmlns=\"DAV:\">" DEBUG_CR);

  /* Are we asking for specific propert(y/ies), or just all of them? */
  if (which_props)
    {
      int n;
      ne_buffer_zappend(body, "<prop>" DEBUG_CR);
      for (n = 0; which_props[n].name != NULL; n++)
        {
          ne_buffer_concat(body, "<", which_props[n].name, " xmlns=\"",
                           which_props[n].nspace, "\"/>" DEBUG_CR, NULL);
        }
      ne_buffer_zappend(body, "</prop></propfind>" DEBUG_CR);
    }
  else
    {
      ne_buffer_zappend(body, "<allprop/></propfind>" DEBUG_CR);
    }

  /* Initialize our baton. */
  memset(&pc, 0, sizeof(pc));
  pc.pool = pool;
  pc.propbuffer = apr_hash_make(pool);
  pc.props = apr_hash_make(pool);

  /* Create and dispatch the request! */
  err = svn_ra_dav__parsed_request(sess, "PROPFIND", url, body->data, 0,
                                   set_parser, propfind_elements,
                                   validate_element,
                                   start_element, end_element,
                                   &pc, extra_headers, NULL, pool);

  ne_buffer_destroy(body);
  *results = pc.props;
  return err;
}

svn_error_t * svn_ra_dav__get_props_resource(svn_ra_dav_resource_t **rsrc,
                                             ne_session *sess,
                                             const char *url,
                                             const char *label,
                                             const ne_propname *which_props,
                                             apr_pool_t *pool)
{
  apr_hash_t *props;
  char * url_path = apr_pstrdup(pool, url);
  int len = strlen(url);
  /* Clean up any trailing slashes. */
  if (len > 1 && url[len - 1] == '/')
      url_path[len - 1] = '\0';

  SVN_ERR( svn_ra_dav__get_props(&props, sess, url_path, NE_DEPTH_ZERO,
                                 label, which_props, pool) );

  /* ### HACK.  We need to have the client canonicalize paths, get rid
     of double slashes and such.  This check is just a check against
     non-SVN servers;  in the long run we want to re-enable this. */
  if (1 || label != NULL)
    {
      /* pick out the first response: the URL requested will not match
       * the response href. */
      apr_hash_index_t *hi = apr_hash_first(pool, props);

      if (hi)
        {
          void *ent;
          apr_hash_this(hi, NULL, NULL, &ent);
          *rsrc = ent;
        }
      else
        *rsrc = NULL;
    }
  else
    {
      *rsrc = apr_hash_get(props, url_path, APR_HASH_KEY_STRING);
    }

  if (*rsrc == NULL)
    {
      /* ### hmmm, should have been in there... */
      return svn_error_createf(APR_EGENERAL, NULL,
                               "failed to find label \"%s\" for URL \"%s\"",
                               label ? label : "NULL", url_path);
    }

  return SVN_NO_ERROR;
}

svn_error_t * svn_ra_dav__get_one_prop(const svn_string_t **propval,
                                       ne_session *sess,
                                       const char *url,
                                       const char *label,
                                       const ne_propname *propname,
                                       apr_pool_t *pool)
{
  svn_ra_dav_resource_t *rsrc;
  ne_propname props[2] = { { 0 } };
  const char *name;
  const svn_string_t *value;

  props[0] = *propname;
  SVN_ERR( svn_ra_dav__get_props_resource(&rsrc, sess, url, label, props,
                                          pool) );

  name = apr_pstrcat(pool, propname->nspace, propname->name, NULL);
  value = apr_hash_get(rsrc->propset, name, APR_HASH_KEY_STRING);
  if (value == NULL)
    {
      /* ### need an SVN_ERR here */
      return svn_error_createf(SVN_ERR_RA_DAV_PROPS_NOT_FOUND, NULL,
                               "'%s' was not present on the resource.", name);
    }

  *propval = value;
  return SVN_NO_ERROR;
}

svn_error_t * svn_ra_dav__get_starting_props(svn_ra_dav_resource_t **rsrc,
                                             ne_session *sess,
                                             const char *url,
                                             const char *label,
                                             apr_pool_t *pool)
{
  return svn_ra_dav__get_props_resource(rsrc, sess, url, label, starting_props,
                                        pool);
}



svn_error_t *
svn_ra_dav__search_for_starting_props(svn_ra_dav_resource_t **rsrc,
                                      const char **missing_path,
                                      ne_session *sess,
                                      const char *url,
                                      apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  apr_size_t len;
  svn_stringbuf_t *path_s;
  const char *parsed_path;
  ne_uri parsed_url;
  const char *lopped_path = "";

  /* Split the url into it's component pieces (schema, host, path,
     etc).  We want the path part. */
  ne_uri_parse (url, &parsed_url);
  path_s = svn_stringbuf_create (parsed_url.path, pool);

  /* Try to get the starting_props from the public url.  If the
     resource no longer exists in HEAD, we'll get a failure.  That's
     fine: just keep removing components and trying to get the
     starting_props from parent directories. */
  while (! svn_path_is_empty (path_s->data))
    {
      err = svn_ra_dav__get_starting_props(rsrc, sess, path_s->data,
                                           NULL, pool);
      if (! err)
        break;   /* found an existing parent! */

      if (err->apr_err != SVN_ERR_RA_DAV_PATH_NOT_FOUND)
        goto error;  /* found a _real_ error */

      /* else... lop off the basename and try again. */
      lopped_path = svn_path_join(svn_path_basename (path_s->data, pool),
                                  lopped_path, pool);
      len = path_s->len;
      svn_path_remove_component(path_s);

      /* if we detect an infinite loop, get out. */
      if (path_s->len == len)
        {
          err = svn_error_quick_wrap(err,
                                     "The path was not part of a repository");
          goto error;
        }
      svn_error_clear (err);
    }

  /* error out if entire URL was bogus (not a single part of it exists
     in the repository!)  */
  if (svn_path_is_empty (path_s->data))
    {
      err = svn_error_createf (SVN_ERR_RA_ILLEGAL_URL, NULL,
                               "No part of path '%s' was found in "
                               "repository HEAD.", parsed_path);
      goto error;
    }

  *missing_path = lopped_path;

 error:
  ne_uri_free(&parsed_url);
  return err;
}


svn_error_t *svn_ra_dav__get_vcc(const char **vcc,
                                 ne_session *sess,
                                 const char *url,
                                 apr_pool_t *pool)
{
  svn_ra_dav_resource_t *rsrc;
  const char *lopped_path;
  const svn_string_t *vcc_s;

  /* ### Someday, possibly look for memory-cached VCC in the RA session. */

  /* ### Someday, possibly look for disk-cached VCC via get_wcprop callback. */

  /* Finally, resort to a set of PROPFINDs up parent directories. */
  SVN_ERR( svn_ra_dav__search_for_starting_props(&rsrc, &lopped_path,
                                                 sess, url, pool) );

  vcc_s = apr_hash_get(rsrc->propset,
                       SVN_RA_DAV__PROP_VCC, APR_HASH_KEY_STRING);
  if (! vcc_s)
    return svn_error_create(APR_EGENERAL, NULL,
                             "The VCC property was not found on the "
                             "resource.");

  *vcc = vcc_s->data;
  return SVN_NO_ERROR;
}


svn_error_t *svn_ra_dav__get_baseline_props(svn_string_t *bc_relative,
                                            svn_ra_dav_resource_t **bln_rsrc,
                                            ne_session *sess,
                                            const char *url,
                                            svn_revnum_t revision,
                                            const ne_propname *which_props,
                                            apr_pool_t *pool)
{
  svn_ra_dav_resource_t *rsrc;
  const svn_string_t *vcc;
  const svn_string_t *relative_path;
  const char *my_bc_relative;
  const char *lopped_path;

  /* ### we may be able to replace some/all of this code with an
     ### expand-property REPORT when that is available on the server. */

  /* -------------------------------------------------------------------
     STEP 1

     Fetch the following properties from the given URL (or, if URL no
     longer exists in HEAD, get the properties from the nearest
     still-existing parent resource):

     *) DAV:version-controlled-configuration so that we can reach the
        baseline information.

     *) svn:baseline-relative-path so that we can find this resource
        within a Baseline Collection.  If we need to search up parent
        directories, then the relative path is this property value
        *plus* any trailing components we had to chop off.

     *) DAV:resourcetype so that we can identify whether this resource
        is a collection or not -- assuming we never had to search up
        parent directories.
  */

  SVN_ERR( svn_ra_dav__search_for_starting_props(&rsrc, &lopped_path,
                                                 sess, url, pool) );

  vcc = apr_hash_get(rsrc->propset, SVN_RA_DAV__PROP_VCC, APR_HASH_KEY_STRING);
  if (vcc == NULL)
    {
      /* ### better error reporting... */

      /* ### need an SVN_ERR here */
      return svn_error_create(APR_EGENERAL, NULL,
                              "The VCC property was not found on the "
                              "resource.");
    }

  /* Allocate our own bc_relative path. */
  relative_path = apr_hash_get(rsrc->propset,
                               SVN_RA_DAV__PROP_BASELINE_RELPATH,
                               APR_HASH_KEY_STRING);
  if (relative_path == NULL)
    {
      /* ### better error reporting... */
      /* ### need an SVN_ERR here */
      return svn_error_create(APR_EGENERAL, NULL,
                              "The relative-path property was not "
                              "found on the resource.");
    }

  /* don't forget to tack on the parts we lopped off in order
     to find the VCC... */
  my_bc_relative = svn_path_join(relative_path->data, lopped_path, pool);

  /* if they want the relative path (could be, they're just trying to find
     the baseline collection), then return it */
  if (bc_relative)
    {
      bc_relative->data = my_bc_relative;
      bc_relative->len = strlen(my_bc_relative);
    }

  /* -------------------------------------------------------------------
     STEP 2

     We have the Version Controlled Configuration (VCC). From here, we
     need to reach the Baseline for specified revision.

     If the revision is SVN_INVALID_REVNUM, then we're talking about
     the HEAD revision. We have one extra step to reach the Baseline:

     *) Fetch the DAV:checked-in from the VCC; it points to the Baseline.

     If we have a specific revision, then we use a Label header when
     fetching props from the VCC. This will direct us to the Baseline
     with that label (in this case, the label == the revision number).

     From the Baseline, we fetch the following properties:

     *) DAV:baseline-collection, which is a complete tree of the Baseline
        (in SVN terms, this tree is rooted at a specific revision)

     *) DAV:version-name to get the revision of the Baseline that we are
        querying. When asking about the HEAD, this tells us its revision.
  */

  if (revision == SVN_INVALID_REVNUM)
    {
      /* Fetch the latest revision */

      const svn_string_t *baseline;

      /* Get the Baseline from the DAV:checked-in value, then fetch its
         DAV:baseline-collection property. */
      /* ### should wrap this with info about rsrc==VCC */
      SVN_ERR( svn_ra_dav__get_one_prop(&baseline, sess, vcc->data, NULL,
                                        &svn_ra_dav__checked_in_prop, pool) );

      /* ### do we want to optimize the props we fetch, based on what the
         ### user asked for? i.e. omit version-name if latest_rev is NULL */
      SVN_ERR( svn_ra_dav__get_props_resource(&rsrc, sess,
                                              baseline->data, NULL,
                                              which_props, pool) );
    }
  else
    {
      /* Fetch a specific revision */

      char label[20];

      /* ### send Label hdr, get DAV:baseline-collection [from the baseline] */

      apr_snprintf(label, sizeof(label), "%" SVN_REVNUM_T_FMT, revision);

      /* ### do we want to optimize the props we fetch, based on what the
         ### user asked for? i.e. omit version-name if latest_rev is NULL */
      SVN_ERR( svn_ra_dav__get_props_resource(&rsrc, sess, vcc->data, label,
                                              which_props, pool) );
    }

  /* Return the baseline rsrc, which now contains whatever set of
     props the caller wanted. */
  *bln_rsrc = rsrc;
  return SVN_NO_ERROR;
}


svn_error_t *svn_ra_dav__get_baseline_info(svn_boolean_t *is_dir,
                                           svn_string_t *bc_url,
                                           svn_string_t *bc_relative,
                                           svn_revnum_t *latest_rev,
                                           ne_session *sess,
                                           const char *url,
                                           svn_revnum_t revision,
                                           apr_pool_t *pool)
{
  svn_ra_dav_resource_t *baseline_rsrc, *rsrc;
  const svn_string_t *my_bc_url;
  svn_string_t my_bc_rel;

  /* Go fetch a BASELINE_RSRC that contains specific properties we
     want.  This routine will also fill in BC_RELATIVE as best it
     can. */
  SVN_ERR (svn_ra_dav__get_baseline_props(&my_bc_rel,
                                          &baseline_rsrc,
                                          sess,
                                          url,
                                          revision,
                                          baseline_props, /* specific props */
                                          pool));

  /* baseline_rsrc now points at the Baseline. We will checkout from
     the DAV:baseline-collection.  The revision we are checking out is
     in DAV:version-name */

  /* Allocate our own copy of bc_url regardless. */
  my_bc_url = apr_hash_get(baseline_rsrc->propset,
                           SVN_RA_DAV__PROP_BASELINE_COLLECTION,
                           APR_HASH_KEY_STRING);
  if (my_bc_url == NULL)
    {
      /* ### better error reporting... */
      /* ### need an SVN_ERR here */
      return svn_error_create(APR_EGENERAL, NULL,
                              "DAV:baseline-collection was not present "
                              "on the baseline resource.");
    }

  /* maybe return bc_url to the caller */
  if (bc_url)
    *bc_url = *my_bc_url;

  if (latest_rev != NULL)
    {
      const svn_string_t *vsn_name= apr_hash_get(baseline_rsrc->propset,
                                                 SVN_RA_DAV__PROP_VERSION_NAME,
                                                 APR_HASH_KEY_STRING);
      if (vsn_name == NULL)
        {
          /* ### better error reporting... */

          /* ### need an SVN_ERR here */
          return svn_error_create(APR_EGENERAL, NULL,
                                  "DAV:version-name was not present on the "
                                  "baseline resource.");
        }
      *latest_rev = SVN_STR_TO_REV(vsn_name->data);
    }

  if (is_dir != NULL)
    {
      /* query the DAV:resourcetype of the full, assembled URL. */
      const char *full_bc_url = svn_path_url_add_component(my_bc_url->data,
                                                           my_bc_rel.data,
                                                           pool);
      SVN_ERR( svn_ra_dav__get_props_resource(&rsrc, sess, full_bc_url,
                                              NULL, starting_props, pool) );
      *is_dir = rsrc->is_collection;
    }

  if (bc_relative)
    *bc_relative = my_bc_rel;

  return SVN_NO_ERROR;
}


/* Helper function for svn_ra_dav__do_proppatch() below. */
static void
do_setprop(ne_buffer *body,
           const char *name,
           const svn_string_t *value,
           apr_pool_t *pool)
{
  const char *encoding = "";
  const char *xml_safe;
  const char *xml_tag_name;

  /* Map property names to namespaces */
#define NSLEN (sizeof(SVN_PROP_PREFIX) - 1)
  if (strncmp(name, SVN_PROP_PREFIX, NSLEN) == 0)
    {
      xml_tag_name = apr_pstrcat(pool, "S:", name + NSLEN, NULL);
    }
#undef NSLEN
  else
    {
      xml_tag_name = apr_pstrcat(pool, "C:", name, NULL);
    }

  /* If there is no value, just generate an empty tag and get outta
     here. */
  if (! value)
    {
      ne_buffer_concat(body, "<", xml_tag_name, "/>", NULL);
      return;
    }

  /* If a property is XML-safe, XML-encode it.  Else, base64-encode
     it. */
  if (svn_xml_is_xml_safe(value->data, value->len))
    {
      svn_stringbuf_t *xml_esc = NULL;
      svn_xml_escape_cdata_string(&xml_esc, value, pool);
      xml_safe = xml_esc->data;
    }
  else
    {
      const svn_string_t *base64ed = svn_base64_encode_string(value, pool);
      encoding = " V:encoding=\"base64\"";
      xml_safe = base64ed->data;
    }

  ne_buffer_concat(body, "<", xml_tag_name, encoding, ">",
                   xml_safe, "</", xml_tag_name, ">", NULL);
  return;
}


svn_error_t *
svn_ra_dav__do_proppatch (svn_ra_session_t *ras,
                          const char *url,
                          apr_hash_t *prop_changes,
                          apr_array_header_t *prop_deletes,
                          apr_pool_t *pool)
{
  ne_request *req;
  int code;
  ne_buffer *body; /* ### using an ne_buffer because it can realloc */
  svn_error_t *err;

  /* just punt if there are no changes to make. */
  if ((prop_changes == NULL || (! apr_hash_count(prop_changes)))
      && (prop_deletes == NULL || prop_deletes->nelts == 0))
    return SVN_NO_ERROR;

  /* easier to roll our own PROPPATCH here than use ne_proppatch(), which
   * doesn't really do anything clever. */
  body = ne_buffer_create();

  ne_buffer_zappend(body,
                    "<?xml version=\"1.0\" encoding=\"utf-8\" ?>" DEBUG_CR
                    "<D:propertyupdate xmlns:D=\"DAV:\" xmlns:V=\""
                    SVN_DAV_PROP_NS_DAV "\" xmlns:C=\""
                    SVN_DAV_PROP_NS_CUSTOM "\" xmlns:S=\""
                    SVN_DAV_PROP_NS_SVN "\">");

  /* Handle property changes. */
  if (prop_changes)
    {
      apr_hash_index_t *hi;
      apr_pool_t *subpool = svn_pool_create(pool);
      ne_buffer_zappend(body, "<D:set><D:prop>");
      for (hi = apr_hash_first(pool, prop_changes); hi; hi = apr_hash_next(hi))
        {
          const void *key;
          void *val;
          apr_hash_this(hi, &key, NULL, &val);
          do_setprop(body, key, val, subpool);
          svn_pool_clear(subpool);
        }
      ne_buffer_zappend(body, "</D:prop></D:set>");
      svn_pool_destroy(subpool);
    }

  /* Handle property deletions. */
  if (prop_deletes)
    {
      int n;
      ne_buffer_zappend(body, "<D:remove><D:prop>");
      for (n = 0; n < prop_deletes->nelts; n++)
        {
          const char *name = APR_ARRAY_IDX(prop_deletes, n, const char *);
          do_setprop(body, name, NULL, pool);
        }
      ne_buffer_zappend(body, "</D:prop></D:remove>");
    }

  /* Finish up the body. */
  ne_buffer_zappend(body, "</D:propertyupdate>");
  req = ne_request_create(ras->sess, "PROPPATCH", url);
  ne_set_request_body_buffer(req, body->data, ne_buffer_size(body));
  ne_add_request_header(req, "Content-Type", "text/xml; charset=UTF-8");

  code = ne_simple_request(ras->sess, req);

  if (code == NE_OK)
    {
      err = SVN_NO_ERROR;
    }
  else
    {
      /* WebDAV spec says that if any part of a PROPPATCH fails, the
         entire 'commit' is rejected.  */
      err = svn_error_create
        (SVN_ERR_RA_DAV_PROPPATCH_FAILED, NULL,
         "At least one property change failed; repository is unchanged.");
    }

  ne_buffer_destroy(body);
  return err;
}



svn_error_t *
svn_ra_dav__do_check_path(void *session_baton,
                          const char *path,
                          svn_revnum_t revision,
                          svn_node_kind_t *kind,
                          apr_pool_t *pool)
{
  svn_ra_session_t *ras = session_baton;
  const char *url = ras->url;
  svn_error_t *err;
  svn_boolean_t is_dir;

  /* ### For now, using svn_ra_dav__get_baseline_info() works because
     we only have three possibilities: dir, file, or none.  When we
     add symlinks, we will need to do something different.  Here's one
     way described by Greg Stein:

       That is a PROPFIND (Depth:0) for the DAV:resourcetype property.

       You can use the svn_ra_dav__get_one_prop() function to fetch
       it. If the PROPFIND fails with a 404, then you have
       svn_node_none. If the resulting property looks like:

           <D:resourcetype>
             <D:collection/>
           </D:resourcetype>

       Then it is a collection (directory; svn_node_dir). Otherwise,
       it is a regular resource (svn_node_file).

       The harder part is parsing the resourcetype property. "Proper"
       parsing means treating it as an XML property and looking for
       the DAV:collection element in there. To do that, however, means
       that get_one_prop() can't be used. I think there may be some
       Neon functions for parsing XML properties; we'd need to
       look. That would probably be the best approach. (an alternative
       is to use apr_xml_* parsing functions on the returned string;
       get back a DOM-like thing, and look for the element).
  */

  /* If we were given a relative path to append, append it. */
  if (path)
    url = svn_path_url_add_component(url, path, pool);

  err = svn_ra_dav__get_baseline_info(&is_dir, NULL, NULL, NULL,
                                      ras->sess, url, revision, pool);

  if (err == SVN_NO_ERROR)
    {
      if (is_dir)
        *kind = svn_node_dir;
      else
        *kind = svn_node_file;
    }
  else if (err->apr_err == SVN_ERR_RA_DAV_PATH_NOT_FOUND)
    {

      svn_error_clear (err);
      *kind = svn_node_none;
      return SVN_NO_ERROR;
    }

  return err;
}
