/*
 * svn_io.h :  general Subversion I/O definitions
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


#ifndef SVN_IO_H
#define SVN_IO_H

#include "svn_types.h"
#include "svn_error.h"
#include "svn_string.h"



/* If PATH exists, set *KIND to the appropriate kind, else set it to
   svn_unknown_kind. */
svn_error_t *svn_io_check_path (svn_string_t *path,
                                enum svn_node_kind *kind,
                                apr_pool_t *pool);


/* Set *TMP_NAME to a unique filename in the same directory as PATH.
 *
 * It doesn't matter if PATH is a file or directory, the tmp name will
 * be in PATH's parent either way.
 *
 * *TMP_NAME will never be exactly the same as PATH, even if PATH does
 * not exist.
 *
 * *TMP_NAME is allocated in POOL.
 *
 * Since there's no guarantee how long the tmp name will remain unique
 * after it is chosen, a part of the name is generated randomly; this
 * makes on-disk collisions less likely, though it can't eliminate
 * them entirely.
 *
 * Avoiding C tmpnam() because not thread-safe.
 * Avoiding C tempname() because it tries standard tmp areas first.
 */
svn_error_t *svn_io_tmp_name (svn_string_t **tmp_name,
                              svn_string_t *path,
                              apr_pool_t *pool);



/* A typedef for functions resembling the POSIX `read' system call,
   representing a incoming stream of bytes, in `caller-pulls' form.

   We will need to compute text deltas for data drawn from files,
   memory, sockets, and so on.  We will need to read tree deltas from
   various sources.  The data may be huge --- too large to read into
   memory at one time.  Using a `read'-like function like this to
   represent the input data allows us to process the data as we go.

   BATON is some opaque structure representing what we're reading.
   Whoever provided the function gets to use BATON however they
   please.

   BUFFER is a buffer to hold the data, and *LEN indicates how many
   bytes to read.  Upon return, the function should set *LEN to the
   number of bytes actually read, or zero at the end of the data
   stream.  *LEN should only change when there is a read error or the
   input stream ends before the full count of bytes can be read; the
   generic read function is obligated to perform a full read when
   possible.

   Any necessary temporary allocation should be done in a sub-pool of
   POOL.  (If the read function needs to perform allocations which
   last beyond the lifetime of the function, it must use a different
   pool, e.g. one referenced through BATON.)  */
typedef svn_error_t *svn_read_fn_t (void *baton,
                                    char *buffer,
                                    apr_size_t *len,
                                    apr_pool_t *pool);

/* Similar to svn_read_fn_t, but for writing.  */
typedef svn_error_t *svn_write_fn_t (void *baton,
				     const char *data,
				     apr_size_t *len,
				     apr_pool_t *pool);


/* A posix-like read function of type svn_read_fn_t (see above).
   Given an already-open APR FILEHANDLE, read LEN bytes into BUFFER.
   (Notice that FILEHANDLE is void *, to match svn_io_read_fn_t).

   As a convenience, if FILEHANDLE is null, then this function will
   set *LEN to 0 and do nothing to BUFFER every time.
*/
svn_error_t *svn_io_file_reader (void *filehandle,
                                 char *buffer,
                                 apr_size_t *len,
                                 apr_pool_t *pool);


/* A posix-like write function of type svn_write_fn_t (see svn_io.h).
   Given an already-open APR FILEHANDLE, write LEN bytes out of BUFFER.
   (Notice that FILEHANDLE is void *, to match svn_io_write_fn_t).
*/
svn_error_t *svn_io_file_writer (void *filehandle,
                                 const char *buffer,
                                 apr_size_t *len,
                                 apr_pool_t *pool);


#endif /* SVN_IO_H */
