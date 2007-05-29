/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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
 * @file LogMessageCallback.h
 * @brief Interface of the class LogMessageCallback
 */

#if !defined(AFX_LOG_MESSAGECALLBACK_H__3BBF3F0F_C80C_45C9_8AC3_E1AF07E5B810__INCLUDED_)
#define AFX_LOG_MESSAGECALLBACK_H__3BBF3F0F_C80C_45C9_8AC3_E1AF07E5B810__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000
#include <jni.h>
#include "svn_client.h"

/**
 * This class holds a Java callback object, which will receive every
 * log message for which the callback information is requested.
 */
class LogMessageCallback
{
 public:
  LogMessageCallback(jobject jcallback);
  ~LogMessageCallback();

  static svn_error_t *callback(void *baton,
                               svn_log_entry_t *log_entry,
                               apr_pool_t *pool);
 protected:
  svn_error_t *singleMessage(svn_log_entry_t *log_entry, apr_pool_t *pool);

 private:
  /**
   * This a local reference to the Java object.
   */
  jobject m_callback;
};

// !defined(AFX_LOG_MESSAGECALLBACK_H__3BBF3F0F_C80C_45C9_8AC3_E1AF07E5B810__INCLUDED_)
#endif
