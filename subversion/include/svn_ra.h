/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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
 * @file svn_ra.h
 * @brief Repository Access
 */




#ifndef SVN_RA_H
#define SVN_RA_H

#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_error.h"
#include "svn_delta.h"
#include "svn_auth.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* Misc. declarations */

/**
 * Get libsvn_ra version information.
 *
 * @since New in 1.1.
 */
const svn_version_t *svn_ra_version (void);


/** This is a function type which allows the RA layer to fetch working
 * copy (WC) properties.
 *
 * The @a baton is provided along with the function pointer and should
 * be passed back in. This will be the @a callback_baton or the
 * @a close_baton as appropriate.
 *
 * @a path is relative to the "root" of the session, defined by the
 * @a repos_url passed to the @c RA->open() vtable call.
 *
 * @a name is the name of the property to fetch. If the property is present,
 * then it is returned in @a value. Otherwise, @a *value is set to @c NULL.
 */
typedef svn_error_t *(*svn_ra_get_wc_prop_func_t) (void *baton,
                                                   const char *relpath,
                                                   const char *name,
                                                   const svn_string_t **value,
                                                   apr_pool_t *pool);

/** This is a function type which allows the RA layer to store new
 * working copy properties during update-like operations.  See the
 * comments for @c svn_ra_get_wc_prop_func_t for @a baton, @a path, and
 * @a name. The @a value is the value that will be stored for the property;
 * a null @a value means the property will be deleted.
 */
typedef svn_error_t *(*svn_ra_set_wc_prop_func_t) (void *baton,
                                                   const char *path,
                                                   const char *name,
                                                   const svn_string_t *value,
                                                   apr_pool_t *pool);

/** This is a function type which allows the RA layer to store new
 * working copy properties as part of a commit.  See the comments for
 * @c svn_ra_get_wc_prop_func_t for @a baton, @a path, and @a name.
 * The @a value is the value that will be stored for the property; a
 * @c NULL @a value means the property will be deleted.
 *
 * Note that this might not actually store the new property before
 * returning, but instead schedule it to be changed as part of
 * post-commit processing (in which case a successful commit means the
 * properties got written).  Thus, during the commit, it is possible
 * to invoke this function to set a new value for a wc prop, then read
 * the wc prop back from the working copy and get the *old* value.
 * Callers beware.
 */
typedef svn_error_t *(*svn_ra_push_wc_prop_func_t) (void *baton,
                                                    const char *path,
                                                    const char *name,
                                                    const svn_string_t *value,
                                                    apr_pool_t *pool);

/** This is a function type which allows the RA layer to invalidate
 * (i.e., remove) wcprops.  See the documentation for
 * @c svn_ra_get_wc_prop_func_t for @a baton, @a path, and @a name.
 *
 * Unlike @c svn_ra_push_wc_prop_func_t, this has immediate effect.  If
 * it returns success, the wcprops have been removed.
 */
typedef svn_error_t *(*svn_ra_invalidate_wc_props_func_t) (void *baton,
                                                           const char *path,
                                                           const char *name,
                                                           apr_pool_t *pool);


/** A function type for retrieving the youngest revision from a repos. */
typedef svn_error_t *(*svn_ra_get_latest_revnum_func_t)
       (void *session_baton,
        svn_revnum_t *latest_revnum);

/**
 * A callback function type for use in @c get_file_revs.
 * @a baton is provided by the caller, @a path is the pathname of the file
 * in revision @a rev and @a rev_props are the revision properties.
 * If @a delta_handler and @a delta_baton are non-NULL, they may be set to a
 * handler/baton which will be called with the delta between the previous
 * revision and this one after the return of this callback.  They may be
 * left as NULL/NULL.
 * @a prop_diffs is an array of svn_prop_t elements indicating the property
 * delta for this and the previous revision.
 * @a pool may be used for temporary allocations, but you can't rely
 * on objects allocated to live outside of this particular call and the
 * immediately following calls to @a *delta_handler, if any.
 *
 * @since New in 1.1.
 */
typedef svn_error_t *(*svn_ra_file_rev_handler_t)
       (void *baton,
        const char *path,
        svn_revnum_t rev,
        apr_hash_t *rev_props,
        svn_txdelta_window_handler_t *delta_handler,
        void **delta_baton,
        apr_array_header_t *prop_diffs,
        apr_pool_t *pool);

/**
 * Callback function type for locking and unlocking actions.
 *
 * @since New in 1.2.
 *
 * @a do_lock is TRUE when locking @a path, and FALSE
 * otherwise.
 *
 * @a lock is a lock for @a path or null if @a do_lock is false or @a ra_err is
 * non-null.
 *
 * @a ra_err is NULL unless the ra layer encounters a locking related
 * error which it passes back for notification purposes.  The caller
 * is responsible for clearing @a ra_err after the callback is run.
 *
 * @a baton is a closure object; it should be provided by the
 * implementation, and passed by the caller.  @a pool may be used for
 * temporary allocation.
 */
typedef svn_error_t *(*svn_ra_lock_callback_t) (void *baton,
                                                const char *path,
                                                svn_boolean_t do_lock,
                                                const svn_lock_t *lock,
                                                svn_error_t *ra_err,
                                                apr_pool_t *pool);

/**
 * Callback function type for progress notification.
 *
 * @a progress is the number of bytes already transferred, @a total is
 * the total number of bytes to transfer or -1 if it's not known, @a
 * baton is the callback baton.
 *
 * @since New in 1.3.
 */
typedef void (*svn_ra_progress_notify_func_t) (apr_off_t progress,
                                               apr_off_t total,
                                               void *baton,
                                               apr_pool_t *pool);


/**
 * The update Reporter.
 *
 * A vtable structure which allows a working copy to describe a subset
 * (or possibly all) of its working-copy to an RA layer, for the
 * purposes of an update, switch, status, or diff operation.
 *
 * Paths for report calls are relative to the target (not the anchor)
 * of the operation.  Report calls must be made in depth-first order:
 * parents before children, all children of a parent before any
 * siblings of the parent.  The first report call must be a set_path
 * with a @a path argument of "" and a valid revision.  (If the target
 * of the operation is locally deleted or missing, use the anchor's
 * revision.)  If the target of the operation is deleted or switched
 * relative to the anchor, follow up the initial set_path call with a
 * link_path or delete_path call with a @a path argument of "" to
 * indicate that.  In no other case may there be two report
 * descriptions for the same path.  If the target of the operation is
 * a locally added file or directory (which previously did not exist),
 * it may be reported as having revision 0 or as having the parent
 * directory's revision.
 *
 * @since New in 1.2.
 */
typedef struct svn_ra_reporter2_t
{
  /** Describe a working copy @a path as being at a particular @a revision.
   *
   * If @a start_empty is set and @a path is a directory, the
   * implementor should assume the directory has no entries or props.
   *
   * This will *override* any previous set_path() calls made on parent
   * paths.  @a path is relative to the URL specified in @c RA->open().
   *
   * If @a lock_token is non-NULL, it is the lock token for @a path in the WC.
   *
   * All temporary allocations are done in @a pool.
   */
  svn_error_t *(*set_path) (void *report_baton,
                            const char *path,
                            svn_revnum_t revision,
                            svn_boolean_t start_empty,
                            const char *lock_token,
                            apr_pool_t *pool);

  /** Describing a working copy @a path as missing.
   *
   * All temporary allocations are done in @a pool.
   */
  svn_error_t *(*delete_path) (void *report_baton,
                               const char *path,
                               apr_pool_t *pool);

  /** Like set_path(), but differs in that @a path in the working copy
   * (relative to the root of the report driver) isn't a reflection of
   * @a path in the repository (relative to the URL specified when
   * opening the RA layer), but is instead a reflection of a different
   * repository @a url at @a revision.
   *
   * If @a start_empty is set and @a path is a directory,
   * the implementor should assume the directory has no entries or props.
   *
   * If @a lock_token is non-NULL, it is the lock token for @a path in the WC.
   *
   * All temporary allocations are done in @a pool.
   */
  svn_error_t *(*link_path) (void *report_baton,
                             const char *path,
                             const char *url,
                             svn_revnum_t revision,
                             svn_boolean_t start_empty,
                             const char *lock_token,
                             apr_pool_t *pool);

  /** WC calls this when the state report is finished; any directories
   * or files not explicitly `set' are assumed to be at the
   * baseline revision originally passed into do_update().  No other
   * reporting functions, including abort_report, should be called after
   * calling this function.
   */
  svn_error_t *(*finish_report) (void *report_baton,
                                 apr_pool_t *pool);

  /** If an error occurs during a report, this routine should cause the
   * filesystem transaction to be aborted & cleaned up.  No other reporting
   * functions should be called after calling this function.
   */
  svn_error_t *(*abort_report) (void *report_baton,
                                apr_pool_t *pool);

} svn_ra_reporter2_t;

/**
 * Similar to @c svn_ra_reporter2_t, but without support for lock tokens.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
typedef struct svn_ra_reporter_t
{
  /** Similar to the corresponding field in @c svn_ra_reporter2_t, but
   * with @a lock_token always set to NULL. */
  svn_error_t *(*set_path) (void *report_baton,
                            const char *path,
                            svn_revnum_t revision,
                            svn_boolean_t start_empty,
                            apr_pool_t *pool);

  /** Same as the corresponding field in @c svn_ra_reporter2_t. */
  svn_error_t *(*delete_path) (void *report_baton,
                               const char *path,
                               apr_pool_t *pool);

  /** Similar to the corresponding field in @c svn_ra_reporter2_t, but
   * with @a lock_token always set to NULL. */
  svn_error_t *(*link_path) (void *report_baton,
                             const char *path,
                             const char *url,
                             svn_revnum_t revision,
                             svn_boolean_t start_empty,
                             apr_pool_t *pool);

  /** Same as the corresponding field in @c svn_ra_reporter2_t. */
  svn_error_t *(*finish_report) (void *report_baton,
                                 apr_pool_t *pool);

  /** Same as the corresponding field in @c svn_ra_reporter2_t. */
  svn_error_t *(*abort_report) (void *report_baton,
                                apr_pool_t *pool);
} svn_ra_reporter_t;


/** A collection of callbacks implemented by libsvn_client which allows
 * an RA layer to "pull" information from the client application, or
 * possibly store information.  libsvn_client passes this vtable to
 * @c RA->open().
 *
 * Each routine takes a @a callback_baton originally provided with the
 * vtable.
 *
 * Clients must use svn_ra_create_callbacks() to allocate and
 * initialize this structure.
 *
 * @since New in 1.3.
 */
typedef struct svn_ra_callbacks2_t
{
  /** Open a unique temporary file for writing in the working copy.
   * This file will be automatically deleted when @a fp is closed.
   */
  svn_error_t *(*open_tmp_file) (apr_file_t **fp,
                                 void *callback_baton,
                                 apr_pool_t *pool);

  /** An authentication baton, created by the application, which is
   * capable of retrieving all known types of credentials.
   */
  svn_auth_baton_t *auth_baton;

  /*** The following items may be set to NULL to disallow the RA layer
       to perform the respective operations of the vtable functions.
       Perhaps WC props are not defined or are in invalid for this
       session, or perhaps the commit operation this RA session will
       perform is a server-side only one that shouldn't do post-commit
       processing on a working copy path.  ***/

  /** Fetch working copy properties.
   *
   *<pre> ### we might have a problem if the RA layer ever wants a property
   * ### that corresponds to a different revision of the file than
   * ### what is in the WC. we'll cross that bridge one day...</pre>
   */
  svn_ra_get_wc_prop_func_t get_wc_prop;

  /** Immediately set new values for working copy properties. */
  svn_ra_set_wc_prop_func_t set_wc_prop;

  /** Schedule new values for working copy properties. */
  svn_ra_push_wc_prop_func_t push_wc_prop;

  /** Invalidate working copy properties. */
  svn_ra_invalidate_wc_props_func_t invalidate_wc_props;

  /** Notification callback used for progress information.
   * May be NULL if not used.
   */
  svn_ra_progress_notify_func_t progress_func;

  /** Notification callback baton, used with progress_func. */
  void *progress_baton;
} svn_ra_callbacks2_t;

/** Similar to svn_ra_callbacks2_t, except that the progress
 * notification function and baton is missing.
 *
 * @deprecated Provided for backward compatibility with the 1.2 API.
 */
typedef struct svn_ra_callbacks_t
{
  svn_error_t *(*open_tmp_file) (apr_file_t **fp,
                                 void *callback_baton,
                                 apr_pool_t *pool);

  svn_auth_baton_t *auth_baton;

  svn_ra_get_wc_prop_func_t get_wc_prop;

  svn_ra_set_wc_prop_func_t set_wc_prop;

  svn_ra_push_wc_prop_func_t push_wc_prop;

  svn_ra_invalidate_wc_props_func_t invalidate_wc_props;

} svn_ra_callbacks_t;



/*----------------------------------------------------------------------*/

/* Public Interfaces. */

/**
 * Initialize the RA library.  This function must be called before using
 * any function in this header, except the deprecated APIs based on
 * @c svn_ra_plugin_t, or svn_ra_version().  This function must not be called
 * simultaneously in multiple threads.  @a pool must live
 * longer than any open RA sessions.
 *
 * @since New in 1.2.
 */
svn_error_t *
svn_ra_initialize (apr_pool_t *pool);

/** Initialize a callback structure.
* Set @a *callbacks to a ra callbacks object, allocated in @a pool.
*
* Clients must use this function to allocate and initialize @c
* svn_ra_callbacks2_t structures.
*
* @since New in 1.3.
*/
svn_error_t *
svn_ra_create_callbacks (svn_ra_callbacks2_t **callbacks,
                         apr_pool_t *pool);

/**
 * A repository access session.  This object is used to perform requests
 * to a repository, identified by an URL.
 *
 * @since New in 1.2.
 */
typedef struct svn_ra_session_t svn_ra_session_t;

/**
 * Open a repository session to @a repos_URL.  Return an opaque object
 * representing this session in @a *session_p, allocated in @a pool.
 *
 * @a callbacks/@a callback_baton is a table of callbacks provided by the
 * client; see @c svn_ra_callbacks2_t.
 *
 * @a config is a hash mapping <tt>const char *</tt> keys to
 * @c svn_config_t * values.  For example, the @c svn_config_t for the
 * "~/.subversion/config" file is under the key "config".
 *
 * All RA requests require a session; they will continue to
 * use @a pool for memory allocation.
 *
 * @see svn_client_open_ra_session().
 *
 * @since New in 1.3.
 */
svn_error_t *svn_ra_open2 (svn_ra_session_t **session_p,
                          const char *repos_URL,
                          const svn_ra_callbacks2_t *callbacks,
                          void *callback_baton,
                          apr_hash_t *config,
                          apr_pool_t *pool);

/**
 * @see svn_ra_open2().
 * @since New in 1.2.
 * @deprecated Provided for backward compatibility with the 1.2 API.
 */
svn_error_t *svn_ra_open (svn_ra_session_t **session_p,
                          const char *repos_URL,
                          const svn_ra_callbacks_t *callbacks,
                          void *callback_baton,
                          apr_hash_t *config,
                          apr_pool_t *pool);

/** Change the root URL of an open @a ra_session to point to a new path in the
 * same repository.  @a url is the new root URL.  Use @a pool for
 * temporary allocations.
 *
 * If @a url has a different repository root than the current session
 * URL, return @c SVN_ERR_RA_ILLEGAL_URL.
 *
 * @since New in 1.4.
 */
svn_error_t *svn_ra_reparent (svn_ra_session_t *ra_session,
                              const char *url,
                              apr_pool_t *pool);

/**
 * Get the latest revision number from the repository of @a session.
 *
 * Use @a pool for memory allocation.
 *
 * @since New in 1.2.
 */
svn_error_t *svn_ra_get_latest_revnum (svn_ra_session_t *session,
                                       svn_revnum_t *latest_revnum,
                                       apr_pool_t *pool);

/**
 * Get the latest revision number at time @a tm in the repository of
 * @a session.
 *
 * Use @a pool for memory allocation.
 *
 * @since New in 1.2.
 */
svn_error_t *svn_ra_get_dated_revision (svn_ra_session_t *session,
                                        svn_revnum_t *revision,
                                        apr_time_t tm,
                                        apr_pool_t *pool);

/**
 * Set the property @a name to @a value on revision @a rev in the repository
 * of @a session.
 *
 * If @a value is @c NULL, delete the named revision property.
 *
 * Please note that properties attached to revisions are @em unversioned.
 *
 * Use @a pool for memory allocation.
 *
 * @since New in 1.2.
 */
svn_error_t *svn_ra_change_rev_prop (svn_ra_session_t *session,
                                     svn_revnum_t rev,
                                     const char *name,
                                     const svn_string_t *value,
                                     apr_pool_t *pool);

/**
 * Set @a *props to the list of unversioned properties attached to revision
 * @a rev in the repository of @a session.  The hash maps
 * (<tt>const char *</tt>) names to (<tt>@c svn_string_t *</tt>) values.
 *
 * Use @a pool for memory allocation.
 *
 * @since New in 1.2.
 */
svn_error_t *svn_ra_rev_proplist (svn_ra_session_t *session,
                                  svn_revnum_t rev,
                                  apr_hash_t **props,
                                  apr_pool_t *pool);

/**
 * Set @a *value to the value of unversioned property @a name attached to
 * revision @a rev in the repository of @a session.  If @a rev has no
 * property by that name, set @a *value to @c NULL.
 *
 * Use @a pool for memory allocation.
 *
 * @since New in 1.2.
 */
svn_error_t *svn_ra_rev_prop (svn_ra_session_t *session,
                              svn_revnum_t rev,
                              const char *name,
                              svn_string_t **value,
                              apr_pool_t *pool);

/**
 * Set @a *editor and @a *edit_baton to an editor for committing changes
 * to the repository of @a session, using @a log_msg as the log message.  The
 * revisions being committed against are passed to the editor
 * functions, starting with the rev argument to @c open_root.  The path
 * root of the commit is in the @a session's URL.
 *
 * Before @c close_edit returns, but after the commit has succeeded,
 * it will invoke @a callback with the new revision number, the
 * commit date (as a <tt>const char *</tt>), commit author (as a
 * <tt>const char *</tt>), and @a callback_baton as arguments.  If
 * @a callback returns an error, that error will be returned from @c
 * close_edit, otherwise @c close_edit will return successfully
 * (unless it encountered an error before invoking @a callback).
 *
 * The callback will not be called if the commit was a no-op
 * (i.e. nothing was committed);
 *
 * @a lock_tokens, if non-NULL, is a hash mapping <tt>const char
 * *</tt> paths (relative to the URL of @a session_baton) to <tt>
 * const char *</tt> lock tokens.  The server checks that the
 * correct token is provided for each committed, locked path.  @a lock_tokens
 * must live during the whole commit operation.
 *
 * If @a keep_locks is @c TRUE, then do not release locks on
 * committed objects.  Else, automatically release such locks.
 *
 * The caller may not perform any RA operations using @a session before
 * finishing the edit.
 *
 * Use @a pool for memory allocation.
 *
 * @since New in 1.4.
 */
svn_error_t *svn_ra_get_commit_editor2 (svn_ra_session_t *session,
                                        const svn_delta_editor_t **editor,
                                        void **edit_baton,
                                        const char *log_msg,
                                        svn_commit_callback2_t callback,
                                        void *callback_baton,
                                        apr_hash_t *lock_tokens,
                                        svn_boolean_t keep_locks,
                                        apr_pool_t *pool);

/**
 * Same as svn_ra_get_commit_editor2(), but uses @c svn_commit_callback_t.
 *
 * @since New in 1.2.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 */
svn_error_t *svn_ra_get_commit_editor (svn_ra_session_t *session,
                                       const svn_delta_editor_t **editor,
                                       void **edit_baton,
                                       const char *log_msg,
                                       svn_commit_callback_t callback,
                                       void *callback_baton,
                                       apr_hash_t *lock_tokens,
                                       svn_boolean_t keep_locks,
                                       apr_pool_t *pool);

/**
 * Fetch the contents and properties of file @a path at @a revision.
 * Interpret @a path relative to the URL in @a session.  Use
 * @a pool for all allocations.
 *
 * If @a revision is @c SVN_INVALID_REVNUM (meaning 'head') and
 * @a *fetched_rev is not @c NULL, then this function will set
 * @a *fetched_rev to the actual revision that was retrieved.  (Some
 * callers want to know, and some don't.)
 *
 * If @a stream is non @c NULL, push the contents of the file at @a
 * stream, do not call svn_stream_close() when finished.
 *
 * If @a props is non @c NULL, set @a *props to contain the properties of
 * the file.  This means @em all properties: not just ones controlled by
 * the user and stored in the repository fs, but non-tweakable ones
 * generated by the SCM system itself (e.g. 'wcprops', 'entryprops',
 * etc.)  The keys are <tt>const char *</tt>, values are
 * <tt>@c svn_string_t *</tt>.
 *
 * The stream handlers for @a stream may not perform any RA
 * operations using @a session.
 *
 * @since New in 1.2.
 */
svn_error_t *svn_ra_get_file (svn_ra_session_t *session,
                              const char *path,
                              svn_revnum_t revision,
                              svn_stream_t *stream,
                              svn_revnum_t *fetched_rev,
                              apr_hash_t **props,
                              apr_pool_t *pool);

/**
 * If @a dirents is non @c NULL, set @a *dirents to contain all the entries
 * of directory @a path at @a revision.  The keys of @a dirents will be
 * entry names (<tt>const char *</tt>), and the values dirents
 * (<tt>@c svn_dirent_t *</tt>).  Use @a pool for all allocations.
 *
 * @a dirent_fields controls which portions of the <tt>@c svn_dirent_t</tt>
 * objects are filled in.  To have them completely filled in just pass
 * @c SVN_DIRENT_ALL, otherwise pass the bitwise OR of all the @c SVN_DIRENT_
 * fields you would like to have returned to you.
 *
 * @a path is interpreted relative to the URL in @a session.
 *
 * If @a revision is @c SVN_INVALID_REVNUM (meaning 'head') and
 * @a *fetched_rev is not @c NULL, then this function will set
 * @a *fetched_rev to the actual revision that was retrieved.  (Some
 * callers want to know, and some don't.)
 *
 * If @a props is non @c NULL, set @a *props to contain the properties of
 * the directory.  This means @em all properties: not just ones controlled by
 * the user and stored in the repository fs, but non-tweakable ones
 * generated by the SCM system itself (e.g. 'wcprops', 'entryprops',
 * etc.)  The keys are <tt>const char *</tt>, values are
 * <tt>@c svn_string_t *</tt>.
 *
 * @since New in 1.4.
 */
svn_error_t *svn_ra_get_dir2 (svn_ra_session_t *session,
                              const char *path,
                              svn_revnum_t revision,
                              apr_uint32_t dirent_fields,
                              apr_hash_t **dirents,
                              svn_revnum_t *fetched_rev,
                              apr_hash_t **props,
                              apr_pool_t *pool);

/**
 * Similar to @c svn_ra_get_dir2, but with @c SVN_DIRENT_ALL for the
 * @a dirent_fields parameter.
 *
 * @since New in 1.2.
 *
 * @deprecated Provided for compatibility with the 1.3 API.
 */
svn_error_t *svn_ra_get_dir (svn_ra_session_t *session,
                             const char *path,
                             svn_revnum_t revision,
                             apr_hash_t **dirents,
                             svn_revnum_t *fetched_rev,
                             apr_hash_t **props,
                             apr_pool_t *pool);

/**
 * Ask the RA layer to update a working copy.
 *
 * The client initially provides an @a update_editor/@a baton to the
 * RA layer; this editor contains knowledge of where the change will
 * begin in the working copy (when @c open_root() is called).
 *
 * In return, the client receives a @a reporter/@a report_baton.  The
 * client then describes its working-copy revision numbers by making
 * calls into the @a reporter structure; the RA layer assumes that all
 * paths are relative to the URL used to open @a session.
 *
 * When finished, the client calls @a reporter->finish_report().  The
 * RA layer then does a complete drive of @a update_editor, ending with
 * close_edit(), to update the working copy.
 *
 * @a update_target is an optional single path component to restrict
 * the scope of the update to just that entry (in the directory
 * represented by the @a session's URL).  If @a update_target is the
 * empty string, the entire directory is updated.
 *
 * If @a recurse is true and the target is a directory, update
 * recursively; otherwise, update just the target and its immediate
 * entries, but not its child directories (if any).
 *
 * The working copy will be updated to @a revision_to_update_to, or the
 * "latest" revision if this arg is invalid.
 *
 * The caller may not perform any RA operations using @a session before
 * finishing the report, and may not perform any RA operations using
 * @a session from within the editing operations of @a update_editor.
 *
 * Use @a pool for memory allocation.
 *
 * @since New in 1.2.
 */
svn_error_t *svn_ra_do_update (svn_ra_session_t *session,
                               const svn_ra_reporter2_t **reporter,
                               void **report_baton,
                               svn_revnum_t revision_to_update_to,
                               const char *update_target,
                               svn_boolean_t recurse,
                               const svn_delta_editor_t *update_editor,
                               void *update_baton,
                               apr_pool_t *pool);

/**
 * Ask the RA layer to 'switch' a working copy to a new
 * @a switch_url;  it's another form of svn_ra_do_update().
 *
 * The client initially provides a @a switch_editor/@a baton to the RA
 * layer; this editor contains knowledge of where the change will
 * begin in the working copy (when open_root() is called).
 *
 * In return, the client receives a @a reporter/@a report_baton.  The
 * client then describes its working-copy revision numbers by making
 * calls into the @a reporter structure; the RA layer assumes that all
 * paths are relative to the URL used to create @a session_baton.
 *
 * When finished, the client calls @a reporter->finish_report().  The
 * RA layer then does a complete drive of @a switch_editor, ending with
 * close_edit(), to switch the working copy.
 *
 * @a switch_target is an optional single path component will restrict
 * the scope of things affected by the switch to an entry in the
 * directory represented by the @a session's URL, or empty if the
 * entire directory is meant to be switched.
 *
 * If @a recurse is true and the target is a directory, switch
 * recursively; otherwise, switch just the target and its immediate
 * entries, but not its child directories (if any).
 *
 * The working copy will be switched to @a revision_to_switch_to, or the
 * "latest" revision if this arg is invalid.
 *
 * The caller may not perform any RA operations using
 * @a session before finishing the report, and may not perform
 * any RA operations using @a session_baton from within the editing
 * operations of @a switch_editor.
 *
 * Use @a pool for memory allocation.
 *
 * @since New in 1.2.
 */
svn_error_t *svn_ra_do_switch (svn_ra_session_t *session,
                               const svn_ra_reporter2_t **reporter,
                               void **report_baton,
                               svn_revnum_t revision_to_switch_to,
                               const char *switch_target,
                               svn_boolean_t recurse,
                               const char *switch_url,
                               const svn_delta_editor_t *switch_editor,
                               void *switch_baton,
                               apr_pool_t *pool);

/**
 * Ask the RA layer to describe the status of a working copy with respect
 * to @a revision of the repository (or HEAD, if @a revision is invalid).
 *
 * The client initially provides a @a status_editor/@a baton to the RA
 * layer; this editor contains knowledge of where the change will
 * begin in the working copy (when open_root() is called).
 *
 * In return, the client receives a @a reporter/@a report_baton. The
 * client then describes its working-copy revision numbers by making
 * calls into the @a reporter structure; the RA layer assumes that all
 * paths are relative to the URL used to open @a session.
 *
 * When finished, the client calls @a reporter->finish_report(). The RA
 * layer then does a complete drive of @a status_editor, ending with
 * close_edit(), to report, essentially, what would be modified in
 * the working copy were the client to call do_update().
 * @a status_target is an optional single path component will restrict
 * the scope of the status report to an entry in the directory
 * represented by the @a session_baton's URL, or empty if the entire
 * directory is meant to be examined.
 *
 * If @a recurse is true and the target is a directory, get status
 * recursively; otherwise, get status for just the target and its
 * immediate entries, but not its child directories (if any).
 *
 * The caller may not perform any RA operations using @a session
 * before finishing the report, and may not perform any RA operations
 * using @a session from within the editing operations of @a status_editor.
 *
 * Use @a pool for memory allocation.
 *
 * @since New in 1.2.
 */
svn_error_t *svn_ra_do_status (svn_ra_session_t *session,
                               const svn_ra_reporter2_t **reporter,
                               void **report_baton,
                               const char *status_target,
                               svn_revnum_t revision,
                               svn_boolean_t recurse,
                               const svn_delta_editor_t *status_editor,
                               void *status_baton,
                               apr_pool_t *pool);

/**
 * Ask the RA layer to 'diff' a working copy against @a versus_url;
 * it's another form of svn_ra_do_update().
 *
 * @note This function cannot be used to diff a single file, only a
 * working copy directory.  See the svn_ra_do_switch() function
 * for more details.
 *
 * The client initially provides a @a diff_editor/@a baton to the RA
 * layer; this editor contains knowledge of where the common diff
 * root is in the working copy (when open_root() is called).
 *
 * In return, the client receives a @a reporter/@a report_baton. The
 * client then describes its working-copy revision numbers by making
 * calls into the @a reporter structure; the RA layer assumes that all
 * paths are relative to the URL used to open @a session.
 *
 * When finished, the client calls @a reporter->finish_report().  The
 * RA layer then does a complete drive of @a diff_editor, ending with
 * close_edit(), to transmit the diff.
 *
 * @a diff_target is an optional single path component will restrict
 * the scope of the diff to an entry in the directory represented by
 * the @a session's URL, or empty if the entire directory is meant to be
 * one of the diff paths.
 *
 * The working copy will be diffed against @a versus_url as it exists
 * in revision @a revision, or as it is in head if @a revision is
 * @c SVN_INVALID_REVNUM.
 *
 * Use @a ignore_ancestry to control whether or not items being
 * diffed will be checked for relatedness first.  Unrelated items
 * are typically transmitted to the editor as a deletion of one thing
 * and the addition of another, but if this flag is @c TRUE,
 * unrelated items will be diffed as if they were related.
 *
 * If @a recurse is true and the target is a directory, diff
 * recursively; otherwise, diff just target and its immediate entries,
 * but not its child directories (if any).
 *
 * The caller may not perform any RA operations using @a session before
 * finishing the report, and may not perform any RA operations using
 * @a session from within the editing operations of @a diff_editor.
 *
 * @a text_deltas instructs the driver of the @a diff_editor to enable
 * the generation of text deltas. If @a text_deltas is FALSE the window
 * handler returned by apply_textdelta will be called once with a NULL
 * @c svn_txdelta_window_t pointer.
 *
 * Use @a pool for memory allocation.
 *
 * @since New in 1.4.
 */
svn_error_t *svn_ra_do_diff2 (svn_ra_session_t *session,
                              const svn_ra_reporter2_t **reporter,
                              void **report_baton,
                              svn_revnum_t revision,
                              const char *diff_target,
                              svn_boolean_t recurse,
                              svn_boolean_t ignore_ancestry,
                              svn_boolean_t text_deltas,
                              const char *versus_url,
                              const svn_delta_editor_t *diff_editor,
                              void *diff_baton,
                              apr_pool_t *pool);

/**
 * Similar to svn_ra_do_diff2(), but with @a text_deltas set to @c TRUE.
 *
 * @deprecated Provided for backward compatibility with the 1.3 API.
 */
svn_error_t *svn_ra_do_diff (svn_ra_session_t *session,
                             const svn_ra_reporter2_t **reporter,
                             void **report_baton,
                             svn_revnum_t revision,
                             const char *diff_target,
                             svn_boolean_t recurse,
                             svn_boolean_t ignore_ancestry,
                             const char *versus_url,
                             const svn_delta_editor_t *diff_editor,
                             void *diff_baton,
                             apr_pool_t *pool);

/**
 * Invoke @a receiver with @a receiver_baton on each log message from
 * @a start to @a end.  @a start may be greater or less than @a end;
 * this just controls whether the log messages are processed in descending
 * or ascending revision number order.
 *
 * If @a start or @a end is @c SVN_INVALID_REVNUM, it defaults to youngest.
 *
 * If @a paths is non-null and has one or more elements, then only show
 * revisions in which at least one of @a paths was changed (i.e., if
 * file, text or props changed; if dir, props changed or an entry
 * was added or deleted).  Each path is an <tt>const char *</tt>, relative
 * to the @a session's common parent.
 *
 * If @a limit is non-zero only invoke @a receiver on the first @a limit
 * logs.
 *
 * If @a discover_changed_paths, then each call to receiver passes a
 * <tt>const apr_hash_t *</tt> for the receiver's @a changed_paths argument;
 * the hash's keys are all the paths committed in that revision.
 * Otherwise, each call to receiver passes null for @a changed_paths.
 *
 * If @a strict_node_history is set, copy history will not be traversed
 * (if any exists) when harvesting the revision logs for each path.
 *
 * If any invocation of @a receiver returns error, return that error
 * immediately and without wrapping it.
 *
 * If @a start or @a end is a non-existent revision, return the error
 * @c SVN_ERR_FS_NO_SUCH_REVISION, without ever invoking @a receiver.
 *
 * See also the documentation for @c svn_log_message_receiver_t.
 *
 * The caller may not invoke any RA operations using @a session from
 * within @a receiver.
 *
 * Use @a pool for memory allocation.
 *
 * @since New in 1.2.
 */
svn_error_t *svn_ra_get_log (svn_ra_session_t *session,
                             const apr_array_header_t *paths,
                             svn_revnum_t start,
                             svn_revnum_t end,
                             int limit,
                             svn_boolean_t discover_changed_paths,
                             svn_boolean_t strict_node_history,
                             svn_log_message_receiver_t receiver,
                             void *receiver_baton,
                             apr_pool_t *pool);

/**
 * Set @a *kind to the node kind associated with @a path at @a revision.
 * If @a path does not exist under @a revision, set @a *kind to
 * @c svn_node_none.  @a path is relative to the @a session's parent URL.
 *
 * Use @a pool for memory allocation.
 *
 * @since New in 1.2.
 */
svn_error_t *svn_ra_check_path (svn_ra_session_t *session,
                                const char *path,
                                svn_revnum_t revision,
                                svn_node_kind_t *kind,
                                apr_pool_t *pool);

/**
 * Set @a *dirent to an @c svn_dirent_t associated with @a path at @a
 * revision.  @a path is relative to the @a session's parent's URL.
 * If @a path does not exist in @a revision, set @a *dirent to NULL.
 *
 * Use @a pool for memory allocation.
 *
 * @since New in 1.2.
 */
svn_error_t *svn_ra_stat (svn_ra_session_t *session,
                          const char *path,
                          svn_revnum_t revision,
                          svn_dirent_t **dirent,
                          apr_pool_t *pool);


/**
 * Set @a *uuid to the repository's UUID.
 *
 * @note The UUID has the same lifetime as the @a session.
 *
 * Use @a pool for temporary memory allocation.
 *
 * @since New in 1.2.
 */
svn_error_t *svn_ra_get_uuid (svn_ra_session_t *session,
                              const char **uuid,
                              apr_pool_t *pool);

/**
 * Set @a *url to the repository's root URL.  The value will not include
 * a trailing '/'.  The returned URL is guaranteed to be a prefix of the
 * @a session's URL.
 *
 * @note The URL has the same lifetime as the @a session.
 *
 * Use @a pool for temporary memory allocation.
 *
 * @since New in 1.2.
 */
svn_error_t *svn_ra_get_repos_root (svn_ra_session_t *session,
                                    const char **url,
                                    apr_pool_t *pool);

/**
 * Set @a *locations to the locations (at the repository revisions
 * @a location_revisions) of the file identified by @a path in
 * @a peg_revision.  @a path is relative to the URL to which
 * @a session was opened.  @a location_revisions is an array of
 * @c svn_revnum_t's.  @a *locations will be a mapping from the revisions to
 * their appropriate absolute paths.  If the file doesn't exist in a
 * location_revision, that revision will be ignored.
 *
 * Use @a pool for all allocations.
 *
 * @note This functionality is not available in pre-1.1 servers.  If the
 * server doesn't implement it, an @c SVN_ERR_RA_NOT_IMPLEMENTED error is
 * returned.
 *
 * @since New in 1.2.
 */
svn_error_t *svn_ra_get_locations (svn_ra_session_t *session,
                                   apr_hash_t **locations,
                                   const char *path,
                                   svn_revnum_t peg_revision,
                                   apr_array_header_t *location_revisions,
                                   apr_pool_t *pool);

/**
 * Retrieve a subset of the interesting revisions of a file @a path
 * as seen in revision @a end (see svn_fs_history_prev() for a
 * definition of "interesting revisions").  Invoke @a handler with
 * @a handler_baton as its first argument for each such revision.
 * @a session is an open RA session.  Use @a pool for all allocations.
 *
 * If there is an interesting revision of the file that is less than or
 * equal to @a start, the iteration will begin at that revision.
 * Else, the iteration will begin at the first revision of the file in
 * the repository, which has to be less than or equal to @a end.  Note
 * that if the function succeeds, @a handler will have been called at
 * least once.
 *
 * In a series of calls to @a handler, the file contents for the first
 * interesting revision will be provided as a text delta against the
 * empty file.  In the following calls, the delta will be against the
 * fulltext contents for the previous call.
 *
 * @note This functionality is not available in pre-1.1 servers.  If the
 * server doesn't implement it, an @c SVN_ERR_RA_NOT_IMPLEMENTED error is
 * returned.
 *
 * @since New in 1.2.
 */
svn_error_t *svn_ra_get_file_revs (svn_ra_session_t *session,
                                   const char *path,
                                   svn_revnum_t start,
                                   svn_revnum_t end,
                                   svn_ra_file_rev_handler_t handler,
                                   void *handler_baton,
                                   apr_pool_t *pool);

/**
 * Lock each path in @a path_revs, which is a hash whose keys are the
 * paths to be locked, and whose values are the corresponding bas
 * revisions for each path.
 *
 * Note that locking is never anonymous, so any server implementing
 * this function will have to "pull" a username from the client, if
 * it hasn't done so already.
 *
 * @a comment is optional: it's either an xml-escapable string
 * which describes the lock, or it is NULL.
 *
 * If any path is already locked by a different user, then call @a
 * lock_func/@a lock_baton with an error.  If @a steal_lock is true,
 * then "steal" the existing lock(s) anyway, even if the RA username
 * does not match the current lock's owner.  Delete any lock on the
 * path, and unconditionally create a new lock.
 *
 * For each path, if its base revision (in @a path_revs) is a valid
 * revnum, then do an out-of-dateness check.  If the revnum is less
 * than the last-changed-revision of any path (or if a path doesn't
 * exist in HEAD), call @a lock_func/@a lock_baton with an
 * SVN_ERR_RA_OUT_OF_DATE error.
 *
 * After successfully locking a file, @a lock_func is called with the
 * @a lock_baton.
 *
 * Use @a pool for temporary allocations.
 *
 * @since New in 1.2.
 */
svn_error_t *svn_ra_lock (svn_ra_session_t *session,
                          apr_hash_t *path_revs,
                          const char *comment,
                          svn_boolean_t steal_lock,
                          svn_ra_lock_callback_t lock_func,
                          void *lock_baton,
                          apr_pool_t *pool);

/**
 * Remove the repository lock for each path in @a path_tokens.
 * @a path_tokens is a hash whose keys are the paths to be locked, and
 * whose values are the corresponding lock tokens for each path.  If
 * the path has no corresponding lock token, or if @a break_lock is TRUE,
 * then the corresponding value shall be "".
 *
 * Note that unlocking is never anonymous, so any server
 * implementing this function will have to "pull" a username from
 * the client, if it hasn't done so already.
 *
 * If @a token points to a lock, but the RA username doesn't match the
 * lock's owner, call @a lockfunc/@a lock_baton with an error.  If @a
 * break_lock is true, however, instead allow the lock to be "broken"
 * by the RA user.
 *
 * After successfully unlocking a path, @a lock_func is called with
 * the @a lock_baton.
 *
 * Use @a pool for temporary allocations.
 *
 * @since New in 1.2.
 */
svn_error_t *svn_ra_unlock (svn_ra_session_t *session,
                            apr_hash_t *path_tokens,
                            svn_boolean_t break_lock,
                            svn_ra_lock_callback_t lock_func,
                            void *lock_baton,
                            apr_pool_t *pool);

/**
 * If @a path is locked, set @a *lock to an svn_lock_t which
 * represents the lock, allocated in @a pool.  If @a path is not
 * locked, set @a *lock to NULL.
 *
 * @since New in 1.2.
 */
svn_error_t *svn_ra_get_lock (svn_ra_session_t *session,
                              svn_lock_t **lock,
                              const char *path,
                              apr_pool_t *pool);

/**
 * Set @a *locks to a hashtable which represents all locks on or
 * below @a path.
 *
 * The hashtable maps (const char *) absolute fs paths to (const
 * svn_lock_t *) structures.  The hashtable -- and all keys and
 * values -- are allocated in @a pool.
 *
 * @note It is not considered an error for @a path to not exist in HEAD.
 * Such a search will simply return no locks.
 *
 * @note This functionality is not available in pre-1.2 servers.  If the
 * server doesn't implement it, an @c SVN_ERR_RA_NOT_IMPLEMENTED error is
 * returned.
 *
 * @since New in 1.2.
 */
svn_error_t *svn_ra_get_locks (svn_ra_session_t *session,
                               apr_hash_t **locks,
                               const char *path,
                               apr_pool_t *pool);


/**
 * Replay the changes from @a revision through @a editor and @a edit_baton.
 *
 * Changes will be limited to those that occur under @a session's URL, and
 * the server will assume that the client has no knowledge of revisions
 * prior to @a low_water_mark.  These two limiting factors define the portion
 * of the tree that the server will assume the client already has knowledge of,
 * and thus any copies of data from outside that part of the tree will be
 * sent in their entirety, not as simple copies or deltas against a previous
 * version.
 *
 * If @a send_deltas is @c TRUE, the actual text and property changes in
 * the revision will be sent, otherwise dummy text deltas and null property
 * changes will be sent instead.
 *
 * @a pool is used for all allocation.
 *
 * @since New in 1.4.
 */
svn_error_t *svn_ra_replay (svn_ra_session_t *session,
                            svn_revnum_t revision,
                            svn_revnum_t low_water_mark,
                            svn_boolean_t send_deltas,
                            const svn_delta_editor_t *editor,
                            void *edit_baton,
                            apr_pool_t *pool);

/**
 * Append a textual list of all available RA modules to the stringbuf
 * @a output.
 *
 * @since New in 1.2.
 */
svn_error_t *svn_ra_print_modules (svn_stringbuf_t *output,
                                   apr_pool_t *pool);


/**
 * Similar to svn_ra_print_modules().
 * @a ra_baton is ignored.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
svn_error_t *svn_ra_print_ra_libraries (svn_stringbuf_t **descriptions,
                                        void *ra_baton,
                                        apr_pool_t *pool);



/**
 * Using this callback struct is similar to calling the newer public
 * interface that is based on @c svn_ra_session_t.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
typedef struct svn_ra_plugin_t
{
  /** The proper name of the RA library, (like "ra_dav" or "ra_local") */
  const char *name;

  /** Short doc string printed out by `svn --version` */
  const char *description;

  /* The vtable hooks */

  /** Call svn_ra_open() and set @a session_baton to an object representing
   * the new session.  All other arguments are passed to svn_ra_open().
   */
  svn_error_t *(*open) (void **session_baton,
                        const char *repos_URL,
                        const svn_ra_callbacks_t *callbacks,
                        void *callback_baton,
                        apr_hash_t *config,
                        apr_pool_t *pool);

  /** Call svn_ra_get_latest_revnum() with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*get_latest_revnum) (void *session_baton,
                                     svn_revnum_t *latest_revnum,
                                     apr_pool_t *pool);

  /** Call svn_ra_get_dated_revision() with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*get_dated_revision) (void *session_baton,
                                      svn_revnum_t *revision,
                                      apr_time_t tm,
                                      apr_pool_t *pool);

  /** Call svn_ra_change_rev_prop() with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*change_rev_prop) (void *session_baton,
                                   svn_revnum_t rev,
                                   const char *name,
                                   const svn_string_t *value,
                                   apr_pool_t *pool);

  /** Call svn_ra_rev_proplist() with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*rev_proplist) (void *session_baton,
                                svn_revnum_t rev,
                                apr_hash_t **props,
                                apr_pool_t *pool);

  /** Call svn_ra_rev_prop() with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*rev_prop) (void *session_baton,
                            svn_revnum_t rev,
                            const char *name,
                            svn_string_t **value,
                            apr_pool_t *pool);

  /** Call svn_ra_get_commit_editor() with the session associated with
   * @a session_baton and all other arguments plus @a lock_tokens set to
   * @c NULL and @a keep_locks set to @c TRUE.
   */
  svn_error_t *(*get_commit_editor) (void *session_baton,
                                     const svn_delta_editor_t **editor,
                                     void **edit_baton,
                                     const char *log_msg,
                                     svn_commit_callback_t callback,
                                     void *callback_baton,
                                     apr_pool_t *pool);

  /** Call svn_ra_get_file() with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*get_file) (void *session_baton,
                            const char *path,
                            svn_revnum_t revision,
                            svn_stream_t *stream,
                            svn_revnum_t *fetched_rev,
                            apr_hash_t **props,
                            apr_pool_t *pool);

  /** Call svn_ra_get_dir() with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*get_dir) (void *session_baton,
                           const char *path,
                           svn_revnum_t revision,
                           apr_hash_t **dirents,
                           svn_revnum_t *fetched_rev,
                           apr_hash_t **props,
                           apr_pool_t *pool);

  /** Call svn_ra_do_update() with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*do_update) (void *session_baton,
                             const svn_ra_reporter_t **reporter,
                             void **report_baton,
                             svn_revnum_t revision_to_update_to,
                             const char *update_target,
                             svn_boolean_t recurse,
                             const svn_delta_editor_t *update_editor,
                             void *update_baton,
                             apr_pool_t *pool);

  /** Call svn_ra_do_switch() with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*do_switch) (void *session_baton,
                             const svn_ra_reporter_t **reporter,
                             void **report_baton,
                             svn_revnum_t revision_to_switch_to,
                             const char *switch_target,
                             svn_boolean_t recurse,
                             const char *switch_url,
                             const svn_delta_editor_t *switch_editor,
                             void *switch_baton,
                             apr_pool_t *pool);

  /** Call svn_ra_do_status() with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*do_status) (void *session_baton,
                             const svn_ra_reporter_t **reporter,
                             void **report_baton,
                             const char *status_target,
                             svn_revnum_t revision,
                             svn_boolean_t recurse,
                             const svn_delta_editor_t *status_editor,
                             void *status_baton,
                             apr_pool_t *pool);

  /** Call svn_ra_do_diff() with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*do_diff) (void *session_baton,
                           const svn_ra_reporter_t **reporter,
                           void **report_baton,
                           svn_revnum_t revision,
                           const char *diff_target,
                           svn_boolean_t recurse,
                           svn_boolean_t ignore_ancestry,
                           const char *versus_url,
                           const svn_delta_editor_t *diff_editor,
                           void *diff_baton,
                           apr_pool_t *pool);

  /** Call svn_ra_get_log() with the session associated with
   * @a session_baton and all other arguments.  @a limit is set to 0.
   */
  svn_error_t *(*get_log) (void *session_baton,
                           const apr_array_header_t *paths,
                           svn_revnum_t start,
                           svn_revnum_t end,
                           svn_boolean_t discover_changed_paths,
                           svn_boolean_t strict_node_history,
                           svn_log_message_receiver_t receiver,
                           void *receiver_baton,
                           apr_pool_t *pool);

  /** Call svn_ra_check_path() with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*check_path) (void *session_baton,
                              const char *path,
                              svn_revnum_t revision,
                              svn_node_kind_t *kind,
                              apr_pool_t *pool);

  /** Call svn_ra_get_uuid() with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*get_uuid) (void *session_baton,
                            const char **uuid,
                            apr_pool_t *pool);

  /** Call svn_ra_get_repos_root() with the session associated with
   * @a session_baton and all other arguments.
   */
  svn_error_t *(*get_repos_root) (void *session_baton,
                                  const char **url,
                                  apr_pool_t *pool);

  /**
   * Call svn_ra_get_locations() with the session associated with
   * @a session_baton and all other arguments.
   *
   * @since New in 1.1.
   */
  svn_error_t *(*get_locations) (void *session_baton,
                                 apr_hash_t **locations,
                                 const char *path,
                                 svn_revnum_t peg_revision,
                                 apr_array_header_t *location_revisions,
                                 apr_pool_t *pool);

  /**
   * Call svn_ra_get_file_revs() with the session associated with
   * @a session_baton and all other arguments.
   *
   * @since New in 1.1.
   */
  svn_error_t *(*get_file_revs) (void *session_baton,
                                 const char *path,
                                 svn_revnum_t start,
                                 svn_revnum_t end,
                                 svn_ra_file_rev_handler_t handler,
                                 void *handler_baton,
                                 apr_pool_t *pool);

  /**
   * Return the plugin's version information.
   *
   * @since New in 1.1.
   */
  const svn_version_t *(*get_version) (void);


} svn_ra_plugin_t;

/**
 * All "ra_FOO" implementations *must* export a function named
 * svn_ra_FOO_init() of type @c svn_ra_init_func_t.
 *
 * When called by libsvn_client, this routine adds an entry (or
 * entries) to the hash table for any URL schemes it handles.  The hash
 * value must be of type (<tt>@c svn_ra_plugin_t *</tt>).  @a pool is a
 * pool for allocating configuration / one-time data.
 *
 * This type is defined to use the "C Calling Conventions" to ensure that
 * abi_version is the first parameter. The RA plugin must check that value
 * before accessing the other parameters.
 *
 * ### need to force this to be __cdecl on Windows... how??
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
typedef svn_error_t *(*svn_ra_init_func_t) (int abi_version,
                                            apr_pool_t *pool,
                                            apr_hash_t *hash);

/**
 * The current ABI (Application Binary Interface) version for the
 * RA plugin model. This version number will change when the ABI
 * between the SVN core (e.g. libsvn_client) and the RA plugin changes.
 *
 * An RA plugin should verify that the passed version number is acceptable
 * before accessing the rest of the parameters, and before returning any
 * information.
 *
 * It is entirely acceptable for an RA plugin to accept multiple ABI
 * versions. It can simply interpret the parameters based on the version,
 * and it can return different plugin structures.
 *
 *
 * <pre>
 * VSN  DATE        REASON FOR CHANGE
 * ---  ----------  ------------------------------------------------
 *   1  2001-02-17  Initial revision.
 *   2  2004-06-29  Preparing for svn 1.1, which adds new RA vtable funcs.
 *      2005-01-19  Rework the plugin interface and don't provide the vtable
 *                  to the client.  Separate ABI versions are no longer used.
 * </pre>
 *
 * @deprecated Provided for backward compatibility with the 1.0 API.
 */
#define SVN_RA_ABI_VERSION      2

/* Public RA implementations. */

/** Initialize libsvn_ra_dav.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API. */
svn_error_t * svn_ra_dav_init (int abi_version,
                               apr_pool_t *pool,
                               apr_hash_t *hash);

/** Initialize libsvn_ra_local.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API. */
svn_error_t * svn_ra_local_init (int abi_version,
                                 apr_pool_t *pool,
                                 apr_hash_t *hash);

/** Initialize libsvn_ra_svn.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API. */
svn_error_t * svn_ra_svn_init (int abi_version,
                               apr_pool_t *pool,
                               apr_hash_t *hash);



/**
 * Initialize the compatibility wrapper, using @a pool for any allocations.
 * The caller must hold on to @a ra_baton as long as the RA library is used.
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
svn_error_t *svn_ra_init_ra_libs (void **ra_baton, apr_pool_t *pool);

/**
 * Return an RA vtable-@a library which can handle URL.  A number of
 * svn_client_* routines will call this internally, but client apps might
 * use it too.  $a ra_baton is a baton obtained by a call to
 * svn_ra_init_ra_libs().
 *
 * @deprecated Provided for backward compatibility with the 1.1 API.
 */
svn_error_t *svn_ra_get_ra_library (svn_ra_plugin_t **library,
                                    void *ra_baton,
                                    const char *url,
                                    apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_RA_H */

