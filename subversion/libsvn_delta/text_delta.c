/*
 * text-delta.c -- Internal text delta representation
 *
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
 */


#include <assert.h>
#include <string.h>

#include <apr_general.h>        /* for APR_INLINE */
#include <apr_md5.h>            /* for, um...MD5 stuff */

#include "svn_delta.h"
#include "svn_io.h"
#include "svn_md5.h"
#include "svn_pools.h"
#include "delta.h"


/* Text delta stream descriptor. */

struct svn_txdelta_stream_t {
  /* These are copied from parameters passed to svn_txdelta. */
  svn_stream_t *source;
  svn_stream_t *target;

  /* Private data */
  svn_boolean_t more;           /* TRUE if there are more data in the pool. */
  svn_filesize_t pos;           /* Offset of next read in source file. */
  char *buf;                    /* Buffer for vdelta data. */

  apr_md5_ctx_t context;        /* APR's MD5 context container. */

  /* Calculated digest from MD5 operations.
     NOTE:  This is only valid after this stream has returned the NULL
     (final) window.  */
  unsigned char digest[APR_MD5_DIGESTSIZE];
};


/* Target-push stream descriptor. */

struct tpush_baton {
  /* These are copied from parameters passed to svn_txdelta_target_push. */
  svn_stream_t *source;
  svn_txdelta_window_handler_t wh;
  void *whb;
  apr_pool_t *pool;

  /* Private data */
  char *buf;
  apr_size_t source_offset;
  apr_size_t source_len;
  svn_boolean_t source_done;
  apr_size_t target_len;
};


/* Text delta applicator.  */

struct apply_baton {
  /* These are copied from parameters passed to svn_txdelta_apply.  */
  svn_stream_t *source;
  svn_stream_t *target;

  /* Private data.  Between calls, SBUF contains the data from the
   * last window's source view, as specified by SBUF_OFFSET and
   * SBUF_LEN.  The contents of TBUF are not interesting between
   * calls.  */
  apr_pool_t *pool;             /* Pool to allocate data from */
  char *sbuf;                   /* Source buffer */
  apr_size_t sbuf_size;         /* Allocated source buffer space */
  svn_filesize_t sbuf_offset;   /* Offset of SBUF data in source stream */
  apr_size_t sbuf_len;          /* Length of SBUF data */
  char *tbuf;                   /* Target buffer */
  apr_size_t tbuf_size;         /* Allocated target buffer space */

  apr_md5_ctx_t md5_context;    /* Leads to result_digest below. */
  unsigned char *result_digest; /* MD5 digest of resultant fulltext;
                                   must point to at least APR_MD5_DIGESTSIZE
                                   bytes of storage. */

  const char *error_info;       /* Optional extra info for error returns. */
};



svn_txdelta_window_t *
svn_txdelta__make_window (const svn_txdelta__ops_baton_t *build_baton,
                          apr_pool_t *pool)
{
  svn_txdelta_window_t *window;
  svn_string_t *new_data = apr_palloc (pool, sizeof (*new_data));

  window = apr_palloc (pool, sizeof (*window));
  window->sview_offset = 0;
  window->sview_len = 0;
  window->tview_len = 0;

  window->num_ops = build_baton->num_ops;
  window->src_ops = build_baton->src_ops;
  window->ops = build_baton->ops;

  /* just copy the fields over, rather than alloc/copying into a whole new
     svn_string_t structure. */
  /* ### would be much nicer if window->new_data were not a ptr... */
  new_data->data = build_baton->new_data->data;
  new_data->len = build_baton->new_data->len;
  window->new_data = new_data;

  return window;
}


/* Compute and return a delta window using the vdelta algorithm on
   DATA, which contains SOURCE_LEN bytes of source data and TARGET_LEN
   bytes of target data.  SOURCE_OFFSET gives the offset of the source
   data, and is simply copied into the window's sview_offset field. */
static svn_txdelta_window_t *
compute_window (const char *data, apr_size_t source_len, apr_size_t target_len,
                apr_size_t source_offset, apr_pool_t *pool)
{
  svn_txdelta__ops_baton_t build_baton = { 0 };
  svn_txdelta_window_t *window;

  /* Compute the delta operations. */
  build_baton.new_data = svn_stringbuf_create ("", pool);
  svn_txdelta__vdelta (&build_baton, data, source_len, target_len, pool);

  /* Create and return the delta window. */
  window = svn_txdelta__make_window (&build_baton, pool);
  window->sview_offset = source_offset;
  window->sview_len = source_len;
  window->tview_len = target_len;
  return window;
}



svn_txdelta_window_t *
svn_txdelta__copy_window (const svn_txdelta_window_t *window,
                          apr_pool_t *pool)
{
  svn_txdelta__ops_baton_t build_baton = { 0 };
  svn_txdelta_window_t *new_window;
  const apr_size_t ops_size = (window->num_ops * sizeof (*build_baton.ops));

  build_baton.num_ops = window->num_ops;
  build_baton.src_ops = window->src_ops;
  build_baton.ops_size = window->num_ops;
  build_baton.ops = apr_palloc (pool, ops_size);
  memcpy (build_baton.ops, window->ops, ops_size);
  build_baton.new_data =
    svn_stringbuf_create_from_string (window->new_data, pool);

  new_window = svn_txdelta__make_window (&build_baton, pool);
  new_window->sview_offset = window->sview_offset;
  new_window->sview_len = window->sview_len;
  new_window->tview_len = window->tview_len;
  return new_window;
}



/* Insert a delta op into a delta window. */

void
svn_txdelta__insert_op (svn_txdelta__ops_baton_t *build_baton,
                        enum svn_delta_action opcode,
                        apr_size_t offset,
                        apr_size_t length,
                        const char *new_data,
                        apr_pool_t *pool)
{
  svn_txdelta_op_t *op;

  /* Check if this op can be merged with the previous op. The vdelta
     algorithm will never generate such ops, but the delta combiner
     can, and this is the obvious place to make the check. */
  if (build_baton->num_ops > 0)
    {
      op = &build_baton->ops[build_baton->num_ops - 1];
      if (op->action_code == opcode
          && (opcode == svn_txdelta_new
              || op->offset + op->length == offset))
        {
          op->length += length;
          if (opcode == svn_txdelta_new)
            svn_stringbuf_appendbytes (build_baton->new_data,
                                       new_data, length);
          return;
        }
    }

  /* Create space for the new op. */
  if (build_baton->num_ops == build_baton->ops_size)
    {
      svn_txdelta_op_t *const old_ops = build_baton->ops;
      int const new_ops_size = (build_baton->ops_size == 0
                                ? 16 : 2 * build_baton->ops_size);
      build_baton->ops =
        apr_palloc (pool, new_ops_size * sizeof (*build_baton->ops));

      /* Copy any existing ops into the new array */
      if (old_ops)
        memcpy (build_baton->ops, old_ops,
                build_baton->ops_size * sizeof (*build_baton->ops));
      build_baton->ops_size = new_ops_size;
    }

  /* Insert the op. svn_delta_source and svn_delta_target are
     just inserted. For svn_delta_new, the new data must be
     copied into the window. */
  op = &build_baton->ops[build_baton->num_ops];
  switch (opcode)
    {
    case svn_txdelta_source:
      ++build_baton->src_ops;
      /*** FALLTHRU ***/
    case svn_txdelta_target:
      op->action_code = opcode;
      op->offset = offset;
      op->length = length;
      break;
    case svn_txdelta_new:
      op->action_code = opcode;
      op->offset = build_baton->new_data->len;
      op->length = length;
      svn_stringbuf_appendbytes (build_baton->new_data, new_data, length);
      break;
    default:
      assert (!"unknown delta op.");
    }

  ++build_baton->num_ops;
}



/* Allocate a delta stream descriptor. */

void
svn_txdelta (svn_txdelta_stream_t **stream,
             svn_stream_t *source,
             svn_stream_t *target,
             apr_pool_t *pool)
{
  *stream = apr_palloc (pool, sizeof (**stream));
  (*stream)->source = source;
  (*stream)->target = target;
  (*stream)->more = TRUE;
  (*stream)->pos = 0;
  (*stream)->buf = apr_palloc (pool, 2 * SVN_STREAM_CHUNK_SIZE);

  /* Initialize MD5 digest calculation. */
  apr_md5_init (&((*stream)->context));
}



/* Pull the next delta window from a stream.

   Our current algorithm for picking source and target views is one
   step up from the dumbest algorithm of "compare corresponding blocks
   of each file."  A problem with that algorithm is that an insertion
   or deletion of N bytes near the beginning of the file will result
   in N bytes of non-overlap in each window from then on.  Our
   algorithm lessens this problem by "padding" the source view with
   half a target view's worth of data on each side.

   For example, suppose the target view size is 16K.  The dumbest
   algorithm would use bytes 0-16K for the first source view, 16-32K
   for the second source view, etc..  Our algorithm uses 0-24K for the
   first source view, 8-40K for the second source view, etc..
   Obviously, we're chewing some extra memory by doubling the source
   view size, but small (less than 8K) insertions or deletions won't
   result in non-overlap in every window.

   If we run out of source data before we run out of target data, we
   reuse the final chunk of data for the remaining windows.  No grand
   scheme at work there; that's just how the code worked out. */
svn_error_t *
svn_txdelta_next_window (svn_txdelta_window_t **window,
                         svn_txdelta_stream_t *stream,
                         apr_pool_t *pool)
{
  svn_error_t *err;
  apr_size_t source_len = SVN_STREAM_CHUNK_SIZE;
  apr_size_t target_len = SVN_STREAM_CHUNK_SIZE;

  /* Read the source stream. */
  err = svn_stream_read (stream->source, stream->buf, &source_len);

  /* Read the target stream. */
  if (err == SVN_NO_ERROR)
    err = svn_stream_read (stream->target, stream->buf + source_len,
                           &target_len);
  if (err != SVN_NO_ERROR)
    return err;
  stream->pos += source_len;

  /* ### The apr_md5 functions always return APR_SUCCESS.  At one
     point, we proposed to APR folks that the interfaces change to
     return void, but for some people that was apparently not a good
     idea, and we didn't bother pressing the matter.  In the meantime,
     we just ignore their return values below. */
  if (target_len == 0)
    {
      /* No target data?  We're done; return the final window. */
      apr_md5_final (stream->digest, &(stream->context));
      *window = NULL;
      stream->more = FALSE;
      return SVN_NO_ERROR;
    }
  else
    {
      apr_md5_update (&(stream->context), stream->buf + source_len,
                      target_len);
    }

  *window = compute_window(stream->buf, source_len, target_len,
                           stream->pos - source_len, pool);

  /* That's it. */
  return SVN_NO_ERROR;
}


const unsigned char *
svn_txdelta_md5_digest (svn_txdelta_stream_t *stream)
{
  /* If there are more windows for this stream, the digest has not yet
     been calculated.  */
  if (stream->more)
    return NULL;

  return stream->digest;
}



/* Functions for implementing a "target push" delta. */

/* This is the write handler for a target-push delta stream.  It reads
 * source data, buffers target data, and fires off delta windows when
 * the target data buffer is full. */
static svn_error_t *
tpush_write_handler (void *baton, const char *data, apr_size_t *len)
{
  struct tpush_baton *tb = baton;
  apr_size_t chunk_len, data_len = *len;
  apr_pool_t *pool = svn_pool_create (tb->pool);
  svn_txdelta_window_t *window;

  while (data_len > 0)
    {
      svn_pool_clear (pool);

      /* Make sure we're all full up on source data, if possible. */
      if (tb->source_len == 0 && !tb->source_done)
        {
          tb->source_len = SVN_STREAM_CHUNK_SIZE;
          SVN_ERR (svn_stream_read (tb->source, tb->buf, &tb->source_len));
          if (tb->source_len < SVN_STREAM_CHUNK_SIZE)
            tb->source_done = TRUE;
        }

      /* Copy in the target data, up to SVN_STREAM_CHUNK_SIZE. */
      chunk_len = SVN_STREAM_CHUNK_SIZE - tb->target_len;
      if (chunk_len > data_len)
        chunk_len = data_len;
      memcpy (tb->buf + tb->source_len + tb->target_len, data, chunk_len);
      data += chunk_len;
      data_len -= chunk_len;
      tb->target_len += chunk_len;

      /* If we're full of target data, compute and fire off a window. */
      if (tb->target_len == SVN_STREAM_CHUNK_SIZE)
        {
          window = compute_window (tb->buf, tb->source_len, tb->target_len,
                                   tb->source_offset, pool);
          SVN_ERR (tb->wh (window, tb->whb));
          tb->source_offset += tb->source_len;
          tb->source_len = 0;
          tb->target_len = 0;
        }
    }

  svn_pool_destroy(pool);
  return SVN_NO_ERROR;
}


/* This is the close handler for a target-push delta stream.  It sends
 * a final window if there is any buffered target data, and then sends
 * a NULL window signifying the end of the window stream. */
static svn_error_t *
tpush_close_handler (void *baton)
{
  struct tpush_baton *tb = baton;
  svn_txdelta_window_t *window;

  /* Send a final window if we have any residual target data. */
  if (tb->target_len > 0)
    {
      window = compute_window(tb->buf, tb->source_len, tb->target_len,
                              tb->source_offset, tb->pool);
      SVN_ERR(tb->wh(window, tb->whb));
    }

  /* Send a final NULL window signifying the end. */
  SVN_ERR(tb->wh(NULL, tb->whb));
  return SVN_NO_ERROR;
}


svn_stream_t *
svn_txdelta_target_push (svn_txdelta_window_handler_t handler,
                         void *handler_baton, svn_stream_t *source,
                         apr_pool_t *pool)
{
  struct tpush_baton *tb;
  svn_stream_t *stream;

  /* Initialize baton. */
  tb = apr_palloc (pool, sizeof(*tb));
  tb->source = source;
  tb->wh = handler;
  tb->whb = handler_baton;
  tb->pool = pool;
  tb->buf = apr_palloc (pool, 2 * SVN_STREAM_CHUNK_SIZE);
  tb->source_offset = 0;
  tb->source_len = 0;
  tb->source_done = FALSE;
  tb->target_len = 0;

  /* Create and return writable stream. */
  stream = svn_stream_create (tb, pool);
  svn_stream_set_write (stream, tpush_write_handler);
  svn_stream_set_close (stream, tpush_close_handler);
  return stream;
}



/* Functions for applying deltas.  */

/* Ensure that BUF has enough space for VIEW_LEN bytes.  */
static APR_INLINE void
size_buffer (char **buf, apr_size_t *buf_size,
             apr_size_t view_len, apr_pool_t *pool)
{
  if (view_len > *buf_size)
    {
      *buf_size *= 2;
      if (*buf_size < view_len)
        *buf_size = view_len;
      *buf = apr_palloc (pool, *buf_size);
    }
}


void
svn_txdelta__apply_instructions (svn_txdelta_window_t *window,
                                 const char *sbuf, char *tbuf,
                                 apr_size_t *tlen)
{
  const svn_txdelta_op_t *op;
  apr_size_t i, j, tpos = 0;

  for (op = window->ops; op < window->ops + window->num_ops; op++)
    {
      const apr_size_t buf_len = (op->length < *tlen - tpos
                                  ? op->length : *tlen - tpos);

      /* Check some invariants common to all instructions.  */
      assert (op->offset >= 0 && op->length >= 0);
      assert (tpos + op->length <= window->tview_len);

      switch (op->action_code)
        {
        case svn_txdelta_source:
          /* Copy from source area.  */
          assert (op->offset + op->length <= window->sview_len);
          memcpy (tbuf + tpos, sbuf + op->offset, buf_len);
          break;

        case svn_txdelta_target:
          /* Copy from target area.  Don't use memcpy() since its
             semantics aren't guaranteed for overlapping memory areas,
             and target copies are allowed to overlap to generate
             repeated data.  */
          assert (op->offset < tpos);
          for (i = op->offset, j = tpos; i < op->offset + buf_len; i++)
            tbuf[j++] = tbuf[i];
          break;

        case svn_txdelta_new:
          /* Copy from window new area.  */
          assert (op->offset + op->length <= window->new_data->len);
          memcpy (tbuf + tpos,
                  window->new_data->data + op->offset,
                  buf_len);
          break;

        default:
          assert (!"Invalid delta instruction code");
        }

      tpos += op->length;
      if (tpos >= *tlen)
        return;                 /* The buffer is full. */
    }

  /* Check that we produced the right amount of data.  */
  assert (tpos == window->tview_len);
  *tlen = tpos;
}


/* Apply WINDOW to the streams given by APPL.  */
static svn_error_t *
apply_window (svn_txdelta_window_t *window, void *baton)
{
  struct apply_baton *ab = (struct apply_baton *) baton;
  apr_size_t len;
  svn_error_t *err;

  if (window == NULL)
    {
      /* We're done; just clean up.  */
      if (ab->result_digest)
        apr_md5_final (ab->result_digest, &(ab->md5_context));

      err = svn_stream_close (ab->target);
      svn_pool_destroy (ab->pool);

      return err;
    }

  /* Make sure the source view didn't slide backwards.  */
  assert (window->sview_len == 0
          || (window->sview_offset >= ab->sbuf_offset
              && (window->sview_offset + window->sview_len
                  >= ab->sbuf_offset + ab->sbuf_len)));

  /* Make sure there's enough room in the target buffer.  */
  size_buffer (&ab->tbuf, &ab->tbuf_size, window->tview_len, ab->pool);

  /* Prepare the source buffer for reading from the input stream.  */
  if (window->sview_offset != ab->sbuf_offset
      || window->sview_len > ab->sbuf_size)
    {
      char *old_sbuf = ab->sbuf;

      /* Make sure there's enough room.  */
      size_buffer (&ab->sbuf, &ab->sbuf_size, window->sview_len, ab->pool);

      /* If the existing view overlaps with the new view, copy the
       * overlap to the beginning of the new buffer.  */
      if (ab->sbuf_offset + ab->sbuf_len > window->sview_offset)
        {
          apr_size_t start =
            (apr_size_t)(window->sview_offset - ab->sbuf_offset);
          memmove (ab->sbuf, old_sbuf + start, ab->sbuf_len - start);
          ab->sbuf_len -= start;
        }
      else
        ab->sbuf_len = 0;
      ab->sbuf_offset = window->sview_offset;
    }

  /* Read the remainder of the source view into the buffer.  */
  if (ab->sbuf_len < window->sview_len)
    {
      len = window->sview_len - ab->sbuf_len;
      err = svn_stream_read (ab->source, ab->sbuf + ab->sbuf_len, &len);
      if (err == SVN_NO_ERROR && len != window->sview_len - ab->sbuf_len)
        err = svn_error_create (SVN_ERR_INCOMPLETE_DATA, NULL,
                                "Delta source ended unexpectedly");
      if (err != SVN_NO_ERROR)
        return err;
      ab->sbuf_len = window->sview_len;
    }

  /* Apply the window instructions to the source view to generate
     the target view.  */
  len = window->tview_len;
  svn_txdelta__apply_instructions (window, ab->sbuf, ab->tbuf, &len);
  assert (len == window->tview_len);

  /* Write out the output. */

  /* ### We've also considered just adding two (optionally null)
     arguments to svn_stream_create(): read_checksum and
     write_checksum.  Then instead of every caller updating an md5
     context when it calls svn_stream_write() or svn_stream_read(),
     streams would do it automatically, and verify the checksum in
     svn_stream_closed().  But this might be overkill for issue #689;
     so for now we just update the context here. */
  if (ab->result_digest)
    apr_md5_update(&(ab->md5_context), ab->tbuf, len);

  return svn_stream_write (ab->target, ab->tbuf, &len);
}


void
svn_txdelta_apply (svn_stream_t *source,
                   svn_stream_t *target,
                   unsigned char *result_digest,
                   const char *error_info,
                   apr_pool_t *pool,
                   svn_txdelta_window_handler_t *handler,
                   void **handler_baton)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  struct apply_baton *ab;

  ab = apr_palloc (subpool, sizeof (*ab));
  ab->source = source;
  ab->target = target;
  ab->pool = subpool;
  ab->sbuf = NULL;
  ab->sbuf_size = 0;
  ab->sbuf_offset = 0;
  ab->sbuf_len = 0;
  ab->tbuf = NULL;
  ab->tbuf_size = 0;
  ab->result_digest = result_digest;

  if (result_digest)
    apr_md5_init (&(ab->md5_context));

  if (error_info)
    ab->error_info = apr_pstrdup (subpool, error_info);
  else
    ab->error_info = NULL;

  *handler = apply_window;
  *handler_baton = ab;
}



/* Convenience routines */

svn_error_t *
svn_txdelta_send_string (const svn_string_t *string,
                         svn_txdelta_window_handler_t handler,
                         void *handler_baton,
                         apr_pool_t *pool)
{
  svn_txdelta_window_t window = { 0 };
  svn_txdelta_op_t op;

  /* Build a single `new' op */
  op.action_code = svn_txdelta_new;
  op.offset = 0;
  op.length = string->len;

  /* Build a single window containing a ptr to the string. */
  window.tview_len = string->len;
  window.num_ops = 1;
  window.ops = &op;
  window.new_data = string;

  /* Push the one window at the handler. */
  SVN_ERR ((*handler) (&window, handler_baton));

  /* Push a NULL at the handler, because we're done. */
  SVN_ERR ((*handler) (NULL, handler_baton));

  return SVN_NO_ERROR;
}

svn_error_t *svn_txdelta_send_stream (svn_stream_t *stream,
                                      svn_txdelta_window_handler_t handler,
                                      void *handler_baton,
                                      unsigned char *digest,
                                      apr_pool_t *pool)
{
  svn_txdelta_stream_t *txstream;
  svn_error_t *err;

  /* ### this is a hack. we should simply read from the stream, construct
     ### some windows, and pass those to the handler. there isn't any reason
     ### to crank up a full "diff" algorithm just to copy a stream.
     ###
     ### will fix RSN. */

  /* Create a delta stream which converts an *empty* bytestream into the
     target bytestream. */
  svn_txdelta (&txstream, svn_stream_empty (pool), stream, pool);
  err = svn_txdelta_send_txstream (txstream, handler, handler_baton, pool);

  if (digest && (! err))
    {
      const unsigned char *result_md5;
      result_md5 = svn_txdelta_md5_digest (txstream);
      /* Since err is null, result_md5 "cannot" be null. */
      memcpy (digest, result_md5, APR_MD5_DIGESTSIZE);
    }

  return err;
}

svn_error_t *svn_txdelta_send_txstream (svn_txdelta_stream_t *txstream,
                                        svn_txdelta_window_handler_t handler,
                                        void *handler_baton,
                                        apr_pool_t *pool)
{
  svn_txdelta_window_t *window;

  /* create a pool just for the windows */
  apr_pool_t *wpool = svn_pool_create (pool);

  do
    {
      /* read in a single delta window */
      SVN_ERR( svn_txdelta_next_window (&window, txstream, wpool));

      /* shove it at the handler */
      SVN_ERR( (*handler)(window, handler_baton));

      /* free the window (if any) */
      svn_pool_clear (wpool);
    }
  while (window != NULL);

  svn_pool_destroy (wpool);

  return SVN_NO_ERROR;
}
