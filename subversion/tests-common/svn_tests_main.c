/*
 * tests-main.c:  shared main() & friends for SVN test-suite programs
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
 * software developed by CollabNet (http://www.CollabNet/)."
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
 * IN NO EVENT SHALL COLLAB.NET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
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



#include <stdio.h>
#include "apr_pools.h"
#include "apr_strings.h"


/* All Subversion test programs include two global arrays: */

/* An array of function pointers (all of our sub-tests) */
extern int (*test_funcs[])();

/* An array of sub-test descriptions */
extern char *descriptions[];


/* ================================================================= */

/* Execute a test number TEST_NUM.  Pretty-print test name and dots
   according to our test-suite spec, and return the result code. */
static int
do_test_num (const char *progname, int test_num, apr_pool_t *pool)
{
  int retval;
  int (*func)();
  int array_size = sizeof(test_funcs)/sizeof(int (*)()) - 1;

  /* Check our array bounds! */
  if ((test_num > array_size) || (test_num <= 0))
    {
      char *msg = (char *) apr_psprintf (pool, "%s %d: NO SUCH TEST",
                                         progname, test_num);
      printf ("FAIL: ");
      printf ("%s", msg);

      return 1;  /* BAIL, this test number doesn't exist. */
    }

  /* Do test */
  func = test_funcs[test_num];
  retval = (*func)();

  if (! retval)
    printf ("PASS: ");
  else
    printf ("FAIL: ");

  /* Pretty print results */
  printf ("%s %s", progname, descriptions[test_num]);

  return retval;
}



/* Standard svn test program */
int
main (int argc, char *argv[])
{
  int test_num;
  int i;
  apr_pool_t *pool;
  int got_error = 0;


  /* How many tests are there? */
  int array_size = sizeof(test_funcs)/sizeof(int (*)()) - 1;

  /* Initialize APR (Apache pools) */
  if (apr_initialize () != APR_SUCCESS)
    {
      printf ("apr_initialize() failed.\n");
      exit (1);
    }
  if (apr_create_pool (&pool, NULL) != APR_SUCCESS)
    {
      printf ("apr_create_pool() failed.\n");
      exit (1);
    }

  /* Notice if there's a command-line argument */
  if (argc >= 2)
    {
      test_num = atoi (argv[1]);
      got_error = do_test_num (argv[0], test_num, pool);
    }
  else /* just run all tests */
    for (i = 1; i <= array_size; i++)
      if (do_test_num (argv[0], i, pool))
        got_error = 1;

  /* Clean up APR */
  apr_destroy_pool (pool);
  apr_terminate();

  return got_error;
}



/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end:
 */

