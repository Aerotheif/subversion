/*
 * options.c :  routines for performing OPTIONS server requests
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */



#include <apr_pools.h>

#include <hip_xml.h>
#include <http_request.h>

#include "svn_error.h"
#include "svn_ra.h"

#include "ra_dav.h"


static const struct hip_xml_elm options_elements[] =
{
  { "DAV:", "activity-collection-set", ELEM_activity_coll_set, 0 },
  { "DAV:", "href", DAV_ELM_href, HIP_XML_CDATA },
  { "DAV:", "options-response", ELEM_options_response, 0 },

  { NULL }
};

typedef struct {
  svn_stringbuf_t *activity_url;
  apr_pool_t *pool;

} options_ctx_t;



static int validate_element(hip_xml_elmid parent, hip_xml_elmid child)
{
  switch (parent)
    {
    case HIP_ELM_root:
      if (child == ELEM_options_response)
        return HIP_XML_VALID;
      else
        return HIP_XML_INVALID;

    case ELEM_options_response:
      if (child == ELEM_activity_coll_set)
        return HIP_XML_VALID;
      else
        return HIP_XML_DECLINE; /* not concerned with other response */

    case ELEM_activity_coll_set:
      if (child == DAV_ELM_href)
        return HIP_XML_VALID;
      else
        return HIP_XML_DECLINE; /* not concerned with unknown crud */

    default:
      return HIP_XML_DECLINE;
    }

  /* NOTREACHED */
}

static int start_element(void *userdata, const struct hip_xml_elm *elm,
                         const char **atts)
{
  /* nothing to do here */
  return 0;
}

static int end_element(void *userdata, const struct hip_xml_elm *elm,
                       const char *cdata)
{
  options_ctx_t *oc = userdata;

  if (elm->id == DAV_ELM_href)
    {
      oc->activity_url = svn_stringbuf_create(cdata, oc->pool);
    }

  return 0;
}

svn_error_t * svn_ra_dav__get_activity_url(svn_stringbuf_t **activity_url,
                                           svn_ra_session_t *ras,
                                           const char *url,
                                           apr_pool_t *pool)
{
  options_ctx_t oc = { 0 };

#if 0
  http_add_response_header_handler(req, "dav",
                                   http_duplicate_header, &dav_header);
#endif

  oc.pool = pool;

  SVN_ERR( svn_ra_dav__parsed_request(ras, "OPTIONS", url,
                                      "<?xml version=\"1.0\" "
                                      "encoding=\"utf-8\"?>"
                                      "<D:options xmlns:D=\"DAV:\">"
                                      "<D:activity-collection-set/>"
                                      "</D:options>", NULL,
                                      options_elements, validate_element,
                                      start_element, end_element, &oc,
                                      pool) );

  if (oc.activity_url == NULL)
    {
      /* ### error */
      return svn_error_create(APR_EGENERAL, 0, NULL, pool,
                              "The OPTIONS response did not include the "
                              "requested activity-collection-set.");
    }

  *activity_url = oc.activity_url;

  return SVN_NO_ERROR;
}


/*
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
