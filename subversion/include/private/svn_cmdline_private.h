/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2008-2009 CollabNet.  All rights reserved.
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
 * @endcopyright
 *
 * @file svn_cmdline_private.h
 * @brief Private functions for Subversion cmdline.
 */

#ifndef SVN_CMDLINE_PRIVATE_H
#define SVN_CMDLINE_PRIVATE_H

#include <apr_pools.h>

#include "svn_string.h"
#include "svn_error.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** Write a property as an XML element into @a *outstr.
 *
 * If @a outstr is NULL, allocate @a *outstr in @a pool; else append to
 * @a *outstr, allocating in @a outstr's pool
 *
 * @a propname is the property name. @a propval is the property value, which
 * will be encoded if it contains unsafe bytes.
 *
 * @since New in 1.6.
 */
void
svn_cmdline__print_xml_prop(svn_stringbuf_t **outstr,
                            const char *propname,
                            svn_string_t *propval,
                            apr_pool_t *pool);


/** An implementation of @c svn_auth_gnome_keyring_unlock_prompt_func_t that
 * prompts the user for default GNOME Keyring password.
 *
 * Expects a @c svn_cmdline_prompt_baton2_t to be passed as @a baton.
 *
 * @since New in 1.6.
 */
svn_error_t *
svn_cmdline__auth_gnome_keyring_unlock_prompt(char **keyring_password,
                                              const char *keyring_name,
                                              void *baton,
                                              apr_pool_t *pool);

/** Container for config options parsed with svn_cmdline__parse_config_option
 *
 * @since New in 1.7.
 */
typedef struct svn_cmdline__config_argument_t
{
  const char *file;
  const char *section;
  const char *option;
  const char *value;
} svn_cmdline__config_argument_t;

/** Parser for 'FILE:SECTION:OPTION=[VALUE]'-style option arguments.
 *
 * Parses @a opt_arg and places its value in @a config_options, an apr array
 * containing svn_cmdline__config_argument_t* elements, allocating the option
 * data in @a pool
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_cmdline__parse_config_option(apr_array_header_t *config_options,
                                 const char *opt_arg,
                                 apr_pool_t *pool);

/** Sets the config options in @a config_options, an apr array containing
 * svn_cmdline__config_argument_t* elements to the configuration in @a cfg,
 * a hash mapping of <tt>const char *</tt> configuration file names to
 * @c svn_config_t *'s. Write warnings to stderr.
 *
 * Use @a prefix as prefix and @a argument_name in warning messages.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_cmdline__apply_config_options(apr_hash_t *config,
                                  apr_array_header_t *config_options,
                                  const char *prefix,
                                  const char *argument_name);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CMDLINE_PRIVATE_H */
