/*
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
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

#include <apr_signal.h>

#include "svn_cmdline.h"
#include "svn_dirent_uri.h"
#include "svn_pools.h"
#include "svn_repos.h"
#include "svn_opt.h"
#include "svn_utf.h"

/* ### Why doesn't SVN_DBG work? */
#if 0
#include "../../subversion/libsvn_subr/debug.c"
#else
#undef SVN_DBG
#define SVN_DBG(x) /* nothing */
#endif

#include "../../subversion/libsvn_fs_fs/fs.h"
#include "../../subversion/libsvn_fs_fs/fs_fs.h"
/* for svn_fs_fs__id_* (used in assertions only) */
#include "../../subversion/libsvn_fs_fs/id.h"

#include "svn_private_config.h"


/** Help messages and version checking. **/

static svn_error_t *
version(apr_pool_t *pool)
{
  return svn_opt_print_help3(NULL, "svn-rep-sharing-stats", TRUE, FALSE, NULL,
                             NULL, NULL, NULL, NULL, NULL, pool);
}

static void
usage(apr_pool_t *pool)
{
  svn_error_clear(svn_cmdline_fprintf
                  (stderr, pool,
                   _("Type 'svn-rep-sharing-stats --help' for usage.\n")));
  exit(1);
}


static void
help(const apr_getopt_option_t *options, apr_pool_t *pool)
{
  svn_error_clear
    (svn_cmdline_fprintf
     (stdout, pool,
      _("usage: svn-rep-sharing-stats [OPTIONS] REPOS_PATH\n\n"
        "  Prints the reference count statistics for representations\n"
        "  in an FSFS repository.\n"
        "\n"
        "  At least one of the options --data/--prop/--both must be specified.\n"
        "\n"
        "Valid options:\n")));
  while (options->description)
    {
      const char *optstr;
      svn_opt_format_option(&optstr, options, TRUE, pool);
      svn_error_clear(svn_cmdline_fprintf(stdout, pool, "  %s\n", optstr));
      ++options;
    }
  svn_error_clear(svn_cmdline_fprintf(stdout, pool, "\n"));
  exit(0);
}


/* Version compatibility check */
static svn_error_t *
check_lib_versions(void)
{
  static const svn_version_checklist_t checklist[] =
    {
      /* ### check FSFS version */
      { "svn_subr",   svn_subr_version },
      { "svn_fs",     svn_fs_version },
      { NULL, NULL }
    };

  SVN_VERSION_DEFINE(my_version);
  return svn_error_return(svn_ver_check_list(&my_version, checklist));
}



/** Cancellation stuff, ### copied from subversion/svn/main.c */

/* A flag to see if we've been cancelled by the client or not. */
static volatile sig_atomic_t cancelled = FALSE;

/* A signal handler to support cancellation. */
static void
signal_handler(int signum)
{
  apr_signal(signum, SIG_IGN);
  cancelled = TRUE;
}

/* Our cancellation callback. */
svn_error_t *
svn_cl__check_cancel(void *baton)
{
  if (cancelled)
    return svn_error_create(SVN_ERR_CANCELLED, NULL, _("Caught signal"));
  else
    return SVN_NO_ERROR;
}

svn_cancel_func_t cancel_func = svn_cl__check_cancel;

void set_up_cancellation()
{
  /* Set up our cancellation support. */
  apr_signal(SIGINT, signal_handler);
#ifdef SIGBREAK
  /* SIGBREAK is a Win32 specific signal generated by ctrl-break. */
  apr_signal(SIGBREAK, signal_handler);
#endif
#ifdef SIGHUP
  apr_signal(SIGHUP, signal_handler);
#endif
#ifdef SIGTERM
  apr_signal(SIGTERM, signal_handler);
#endif

#ifdef SIGPIPE
  /* Disable SIGPIPE generation for the platforms that have it. */
  apr_signal(SIGPIPE, SIG_IGN);
#endif

#ifdef SIGXFSZ
  /* Disable SIGXFSZ generation for the platforms that have it, otherwise
   * working with large files when compiled against an APR that doesn't have
   * large file support will crash the program, which is uncool. */
  apr_signal(SIGXFSZ, SIG_IGN);
#endif
}


/** Program-specific code. **/
enum {
  OPT_VERSION = SVN_OPT_FIRST_LONGOPT_ID,
  OPT_DATA,
  OPT_PROP,
  OPT_BOTH
};

/* Increment records[rep->sha1_checksum] if all of them are non-NULL.
 * If necessary, allocate the cstring key in RESULT_POOL.  */
static svn_error_t *record(apr_hash_t *records,
                           representation_t *rep,
                           apr_pool_t *result_pool)
{
  const char *cstring;
  unsigned int oldvalue, newvalue;

  /* Skip if we ignore this particular kind of reps, or if the rep doesn't
   * exist or doesn't have the checksum we are after.  (The latter case
   * often corresponds to node_rev->kind == svn_node_dir.)
   */
  if (records == NULL || rep == NULL || rep->sha1_checksum == NULL)
    return SVN_NO_ERROR;

  cstring = svn_checksum_to_cstring_display(rep->sha1_checksum, result_pool);
  oldvalue = (unsigned int) apr_hash_get(records, cstring, APR_HASH_KEY_STRING);
  newvalue = oldvalue + 1;
  apr_hash_set(records, cstring, APR_HASH_KEY_STRING, (void *) newvalue);

  return SVN_NO_ERROR;
}

/* Inspect the data and/or prop reps of revision REVNUM in FS.  Store
 * reference count tallies in passed hashes (allocated in RESULT_POOL),
 * as maps of const char * checksum cstrings to unsigned ints.
 *
 * If PROP_REPS or DATA_REPS is NULL, the respective kind of reps are not
 * tallied.
 *
 * Use SCRATCH_POOL for temporary allocations.
 */
static svn_error_t *
process_one_revision(svn_fs_t *fs,
                     svn_revnum_t revnum,
                     apr_hash_t *prop_reps,
                     apr_hash_t *data_reps,
                     apr_hash_t *both_reps,
                     apr_pool_t *scratch_pool,
                     apr_pool_t *result_pool)
{
  svn_fs_root_t *rev_root;
  apr_hash_t *paths_changed;
  apr_hash_index_t *hi;

  SVN_DBG(("processing r%ld\n", revnum));

  /* Get the changed paths. */
  SVN_ERR(svn_fs_revision_root(&rev_root, fs, revnum, scratch_pool));
  SVN_ERR(svn_fs_paths_changed2(&paths_changed, rev_root, scratch_pool));

  /* Iterate them. */
  for (hi = apr_hash_first(scratch_pool, paths_changed);
       hi; hi = apr_hash_next(hi))
    {
      const char *path;
      const svn_fs_path_change2_t *change;
      const svn_fs_id_t *node_rev_id1, *node_rev_id2;
      const svn_fs_id_t *the_id;

      node_revision_t *node_rev;

      path = svn_apr_hash_index_key(hi);
      change = svn_apr_hash_index_val(hi);
      SVN_DBG(("processing r%ld:%s\n", revnum, path));

      if (change->change_kind == svn_fs_path_change_delete)
        /* Can't ask for reps of PATH at REVNUM if the path no longer exists
         * at that revision! */
        continue;

      /* Okay, we have two node_rev id's for this change: the txn one and
       * the revision one.  We'll use the latter. */
      node_rev_id1 = change->node_rev_id;
      SVN_ERR(svn_fs_node_id(&node_rev_id2, rev_root, path, scratch_pool));

      SVN_ERR_ASSERT(svn_fs_fs__id_txn_id(node_rev_id1) != NULL);
      SVN_ERR_ASSERT(svn_fs_fs__id_rev(node_rev_id2) != SVN_INVALID_REVNUM);

      the_id = node_rev_id2;

      /* Get the node_rev using the chosen node_rev_id. */
      SVN_ERR(svn_fs_fs__get_node_revision(&node_rev, fs, the_id, scratch_pool));

      /* Maybe record the sha1's. */
      SVN_ERR(record(prop_reps, node_rev->prop_rep, result_pool));
      SVN_ERR(record(data_reps, node_rev->data_rep, result_pool));
      SVN_ERR(record(both_reps, node_rev->prop_rep, result_pool));
      SVN_ERR(record(both_reps, node_rev->data_rep, result_pool));
    }

  return SVN_NO_ERROR;
}

/* Print REPS_REF_COUNT (a hash mapping const char * keys to
 * unsigned ints) to stdout in "value => key" format (for sorting,
 * since the keys are just sha1's).  Prepend each line by NAME.
 *
 * Use SCRATCH_POOL for temporary allocations.
 */
svn_error_t *pretty_print(const char *name,
                          apr_hash_t *reps_ref_counts,
                          apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  if (reps_ref_counts == NULL)
    return SVN_NO_ERROR;

  for (hi = apr_hash_first(scratch_pool, reps_ref_counts);
       hi; hi = apr_hash_next(hi))
    {
      const char *sha1_cstring = svn_apr_hash_index_key(hi);
      unsigned int ref_count = (unsigned int) svn_apr_hash_index_val(hi);

      SVN_ERR(cancel_func(NULL));
      SVN_ERR(svn_cmdline_printf(scratch_pool, "%s %u %s\n",
                                 name, ref_count, sha1_cstring));
    }

  return SVN_NO_ERROR;
}

/* Return an error unless FS is an fsfs fs. */
static svn_error_t *is_fs_fsfs(svn_fs_t *fs, apr_pool_t *scratch_pool)
{
  const char *actual, *expected, *path;

  path = svn_fs_path(fs, scratch_pool);

  expected = SVN_FS_TYPE_FSFS;
  SVN_ERR(svn_fs_type(&actual, path, scratch_pool));

  if (strcmp(actual, expected) != 0)
    return svn_error_createf(SVN_ERR_FS_UNKNOWN_FS_TYPE, NULL,
                             "Filesystem '%s' is not of type '%s'",
                             svn_dirent_local_style(path, scratch_pool),
                             actual);

  return SVN_NO_ERROR;
}

/* The core logic.  This function iterates the repository REPOS_PATH
 * and sends all the (DATA and/or PROP) reps in each revision for counting
 * by process_one_revision().
 */
static svn_error_t *process(const char *repos_path,
                            svn_boolean_t prop,
                            svn_boolean_t data,
                            apr_pool_t *scratch_pool)
{
  apr_hash_t *prop_reps = NULL;
  apr_hash_t *data_reps = NULL;
  apr_hash_t *both_reps = NULL;
  svn_revnum_t rev, youngest;
  apr_pool_t *iterpool;
  svn_repos_t *repos;
  svn_fs_t *fs;

  if (prop)
    prop_reps = apr_hash_make(scratch_pool);
  if (data)
    data_reps = apr_hash_make(scratch_pool);
  if (prop && data)
    both_reps = apr_hash_make(scratch_pool);

  /* Open the FS. */
  SVN_ERR(svn_repos_open(&repos, repos_path, scratch_pool));
  fs = svn_repos_fs(repos);

  SVN_ERR(is_fs_fsfs(fs, scratch_pool));

  SVN_ERR(svn_fs_youngest_rev(&youngest, fs, scratch_pool));

  /* Iterate the revisions. */
  iterpool = svn_pool_create(scratch_pool);
  for (rev = 0; rev <= youngest; rev++)
    {
      svn_pool_clear(iterpool);
      SVN_ERR(cancel_func(NULL));
      SVN_ERR(process_one_revision(fs, rev, prop_reps, data_reps, both_reps,
                                   iterpool, scratch_pool));
    }
  svn_pool_destroy(iterpool);

  /* Print stats. */
  SVN_ERR(pretty_print("prop", prop_reps, scratch_pool));
  SVN_ERR(pretty_print("data", data_reps, scratch_pool));
  SVN_ERR(pretty_print("both", both_reps, scratch_pool));

  return SVN_NO_ERROR;
}

/*
 * Why is this not an svn subcommand?  I have this vague idea that it could
 * be run as part of the build process, with the output embedded in the svn
 * program.  Obviously we don't want to have to run svn when building svn.
 */
int
main(int argc, const char *argv[])
{
  const char *repos_path;
  apr_allocator_t *allocator;
  apr_pool_t *pool;
  svn_boolean_t prop = FALSE, data = FALSE;
  svn_error_t *err;
  apr_getopt_t *os;
  const apr_getopt_option_t options[] =
    {
      {"data", OPT_DATA, 0, N_("display data reps stats")},
      {"prop", OPT_PROP, 0, N_("display prop reps stats")},
      {"both", OPT_BOTH, 0, N_("display combined (data+prop) reps stats")},
      {"help", 'h', 0, N_("display this help")},
      {"version", OPT_VERSION, 0,
       N_("show program version information")},
      {0,             0,  0,  0}
    };

  /* Initialize the app. */
  if (svn_cmdline_init("svn-rep-sharing-stats", stderr) != EXIT_SUCCESS)
    return EXIT_FAILURE;

  /* Create our top-level pool.  Use a separate mutexless allocator,
   * given this application is single threaded.
   */
  if (apr_allocator_create(&allocator))
    return EXIT_FAILURE;

  apr_allocator_max_free_set(allocator, SVN_ALLOCATOR_RECOMMENDED_MAX_FREE);

  pool = svn_pool_create_ex(NULL, allocator);
  apr_allocator_owner_set(allocator, pool);

  /* Check library versions */
  err = check_lib_versions();
  if (err)
    return svn_cmdline_handle_exit_error(err, pool, "svn-rep-sharing-stats: ");

  err = svn_cmdline__getopt_init(&os, argc, argv, pool);
  if (err)
    return svn_cmdline_handle_exit_error(err, pool, "svn-rep-sharing-stats: ");

  os->interleave = 1;
  while (1)
    {
      int opt;
      const char *arg;
      apr_status_t status = apr_getopt_long(os, options, &opt, &arg);
      if (APR_STATUS_IS_EOF(status))
        break;
      if (status != APR_SUCCESS)
        {
          usage(pool);
          return EXIT_FAILURE;
        }
      switch (opt)
        {
        case OPT_DATA:
          data = TRUE;
          break;
        case OPT_PROP:
          prop = TRUE;
          break;
        case OPT_BOTH:
          data = TRUE;
          prop = TRUE;
          break;
        case 'h':
          help(options, pool);
          break;
        case OPT_VERSION:
          SVN_INT_ERR(version(pool));
          exit(0);
          break;
        default:
          usage(pool);
          return EXIT_FAILURE;
        }
    }

  /* Exactly 1 non-option argument,
   * and at least one of "--data"/"--prop"/"--both".
   */
  if (os->ind + 1 != argc || (!data && !prop))
    {
      usage(pool);
      return EXIT_FAILURE;
    }

  /* Grab REPOS_PATH from argv. */
  SVN_INT_ERR(svn_utf_cstring_to_utf8(&repos_path, os->argv[os->ind], pool));
  repos_path = svn_dirent_internal_style(repos_path, pool);

  set_up_cancellation();

  /* Do something. */
  SVN_INT_ERR(process(repos_path, prop, data, pool));

  /* We're done. */

  svn_pool_destroy(pool);
  /* Flush stdout to make sure that the user will see any printing errors. */
  SVN_INT_ERR(svn_cmdline_fflush(stdout));

  return EXIT_SUCCESS;
}
