/**
 * @copyright
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
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
 * @endcopyright
 *
 * @file ConflictResolverCallback.cpp
 * @brief Implementation of the class ConflictResolverCallback.
 */

#include "svn_error.h"

#include "JNIUtil.h"
#include "JNIStringHolder.h"
#include "EnumMapper.h"
#include "RevisionRange.h"
#include "CreateJ.h"
#include "../include/org_apache_subversion_javahl_types_Revision.h"
#include "../include/org_apache_subversion_javahl_CommitItemStateFlags.h"

#include "svn_path.h"
#include "private/svn_wc_private.h"

jobject
CreateJ::ConflictDescriptor(const svn_wc_conflict_description_t *desc)
{
  JNIEnv *env = JNIUtil::getEnv();

  if (desc == NULL)
    return NULL;

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  // Create an instance of the conflict descriptor.
  jclass clazz = env->FindClass(JAVA_PACKAGE "/ConflictDescriptor");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static jmethodID ctor = 0;
  if (ctor == 0)
    {
      ctor = env->GetMethodID(clazz, "<init>", "(Ljava/lang/String;"
                              "L"JAVA_PACKAGE"/ConflictDescriptor$Kind;"
                              "L"JAVA_PACKAGE"/types/NodeKind;"
                              "Ljava/lang/String;ZLjava/lang/String;"
                              "L"JAVA_PACKAGE"/ConflictDescriptor$Action;"
                              "L"JAVA_PACKAGE"/ConflictDescriptor$Reason;"
                              "L"JAVA_PACKAGE"/ConflictDescriptor$Operation;"
                              "Ljava/lang/String;Ljava/lang/String;"
                              "Ljava/lang/String;Ljava/lang/String;"
                              "L"JAVA_PACKAGE"/types/ConflictVersion;"
                              "L"JAVA_PACKAGE"/types/ConflictVersion;)V");
      if (JNIUtil::isJavaExceptionThrown() || ctor == 0)
        POP_AND_RETURN_NULL;
    }

  jstring jpath = JNIUtil::makeJString(desc->path);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jpropertyName = JNIUtil::makeJString(desc->property_name);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jmimeType = JNIUtil::makeJString(desc->mime_type);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jbasePath = JNIUtil::makeJString(desc->base_file);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jreposPath = JNIUtil::makeJString(desc->their_file);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring juserPath = JNIUtil::makeJString(desc->my_file);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jmergedPath = JNIUtil::makeJString(desc->merged_file);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jobject jsrcLeft = CreateJ::ConflictVersion(desc->src_left_version);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jobject jsrcRight = ConflictVersion(desc->src_right_version);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jobject jnodeKind = EnumMapper::mapNodeKind(desc->node_kind);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jobject jconflictKind = EnumMapper::mapConflictKind(desc->kind);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jobject jconflictAction = EnumMapper::mapConflictAction(desc->action);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jobject jconflictReason = EnumMapper::mapConflictReason(desc->reason);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jobject joperation = EnumMapper::mapOperation(desc->operation);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  // Instantiate the conflict descriptor.
  jobject jdesc = env->NewObject(clazz, ctor, jpath, jconflictKind,
                                 jnodeKind, jpropertyName,
                                 (jboolean) desc->is_binary, jmimeType,
                                 jconflictAction, jconflictReason, joperation,
                                 jbasePath, jreposPath, juserPath,
                                 jmergedPath, jsrcLeft, jsrcRight);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  return env->PopLocalFrame(jdesc);
}

jobject
CreateJ::ConflictVersion(const svn_wc_conflict_version_t *version)
{
  JNIEnv *env = JNIUtil::getEnv();

  if (version == NULL)
    return NULL;

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  // Create an instance of the conflict version.
  jclass clazz = env->FindClass(JAVA_PACKAGE "/types/ConflictVersion");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static jmethodID ctor = 0;
  if (ctor == 0)
    {
      ctor = env->GetMethodID(clazz, "<init>", "(Ljava/lang/String;J"
                                               "Ljava/lang/String;"
                                               "L"JAVA_PACKAGE"/types/NodeKind;"
                                               ")V");
      if (JNIUtil::isJavaExceptionThrown() || ctor == 0)
        POP_AND_RETURN_NULL;
    }

  jstring jreposURL = JNIUtil::makeJString(version->repos_url);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jpathInRepos = JNIUtil::makeJString(version->path_in_repos);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jobject jnodeKind = EnumMapper::mapNodeKind(version->node_kind);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jversion = env->NewObject(clazz, ctor, jreposURL,
                                    (jlong)version->peg_rev, jpathInRepos,
                                    jnodeKind);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  return env->PopLocalFrame(jversion);
}

jobject
CreateJ::Info(const char *path, const svn_info2_t *info)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jclass clazz = env->FindClass(JAVA_PACKAGE "/types/Info");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static jmethodID mid = 0;
  if (mid == 0)
    {
      mid = env->GetMethodID(clazz, "<init>",
                             "(Ljava/lang/String;Ljava/lang/String;"
                             "Ljava/lang/String;J"
                             "L"JAVA_PACKAGE"/types/NodeKind;"
                             "Ljava/lang/String;Ljava/lang/String;"
                             "JJLjava/lang/String;"
                             "L"JAVA_PACKAGE"/types/Lock;Z"
                             "L"JAVA_PACKAGE"/types/Info$ScheduleKind;"
                             "Ljava/lang/String;JJLjava/lang/String;"
                             "Ljava/lang/String;JJ"
                             "L"JAVA_PACKAGE"/types/Depth;Ljava/util/Set;)V");
      if (mid == 0 || JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  jstring jpath = JNIUtil::makeJString(path);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jwcroot = NULL;
  jstring jcopyFromUrl = NULL;
  jstring jchecksum = NULL;
  jstring jchangelist = NULL;
  jobject jconflicts = NULL;
  jobject jscheduleKind = NULL;
  jobject jdepth = NULL;
  jlong jworkingSize = -1;
  jlong jcopyfrom_rev = -1;
  jlong jtext_time = -1;
  if (info->wc_info)
    {
      jwcroot = JNIUtil::makeJString(info->wc_info->wcroot_abspath);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      jcopyFromUrl = JNIUtil::makeJString(info->wc_info->copyfrom_url);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      jchecksum = JNIUtil::makeJString(info->wc_info->checksum);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      jchangelist = JNIUtil::makeJString(info->wc_info->changelist);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      jconflicts = NULL;
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      jscheduleKind = EnumMapper::mapScheduleKind(info->wc_info->schedule);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      jdepth = EnumMapper::mapDepth(info->wc_info->depth);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      jworkingSize = info->wc_info->working_size;
      jcopyfrom_rev = info->wc_info->copyfrom_rev;
      jtext_time = info->wc_info->text_time;
    }

  jstring jurl = JNIUtil::makeJString(info->URL);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jreposRootUrl = JNIUtil::makeJString(info->repos_root_URL);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jreportUUID = JNIUtil::makeJString(info->repos_UUID);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jlastChangedAuthor =
    JNIUtil::makeJString(info->last_changed_author);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jlock = CreateJ::Lock(info->lock);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jnodeKind = EnumMapper::mapNodeKind(info->kind);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jinfo2 = env->NewObject(clazz, mid, jpath, jwcroot, jurl,
                                  (jlong) info->rev,
                                  jnodeKind, jreposRootUrl, jreportUUID,
                                  (jlong) info->last_changed_rev,
                                  (jlong) info->last_changed_date,
                                  jlastChangedAuthor, jlock,
                                  info->wc_info ? JNI_TRUE : JNI_FALSE,
                                  jscheduleKind, jcopyFromUrl,
                                  jcopyfrom_rev, jtext_time, jchecksum,
                                  jchangelist, jworkingSize,
                                  (jlong) info->size, jdepth, NULL);

  return env->PopLocalFrame(jinfo2);
}

jobject
CreateJ::Lock(const svn_lock_t *lock)
{
  if (lock == NULL)
    return NULL;

  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jclass clazz = env->FindClass(JAVA_PACKAGE"/types/Lock");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static jmethodID mid = 0;
  if (mid == 0)
    {
      mid = env->GetMethodID(clazz, "<init>",
                             "(Ljava/lang/String;Ljava/lang/String;"
                             "Ljava/lang/String;"
                             "Ljava/lang/String;JJ)V");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  jstring jOwner = JNIUtil::makeJString(lock->owner);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jPath = JNIUtil::makeJString(lock->path);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jToken = JNIUtil::makeJString(lock->token);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;
  jstring jComment = JNIUtil::makeJString(lock->comment);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jlong jCreationDate = lock->creation_date;
  jlong jExpirationDate = lock->expiration_date;
  jobject jlock = env->NewObject(clazz, mid, jOwner, jPath, jToken, jComment,
                                 jCreationDate, jExpirationDate);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  return env->PopLocalFrame(jlock);
}

jobject
CreateJ::ChangedPath(const char *path, svn_log_changed_path2_t *log_item)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jclass clazzCP = env->FindClass(JAVA_PACKAGE"/types/ChangePath");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN(SVN_NO_ERROR);

  static jmethodID midCP = 0;
  if (midCP == 0)
    {
      midCP = env->GetMethodID(clazzCP,
                               "<init>",
                               "(Ljava/lang/String;JLjava/lang/String;"
                               "L"JAVA_PACKAGE"/types/ChangePath$Action;"
                               "L"JAVA_PACKAGE"/types/NodeKind;"
                               "L"JAVA_PACKAGE"/types/Tristate;"
                               "L"JAVA_PACKAGE"/types/Tristate;)V");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN(SVN_NO_ERROR);
    }

  jstring jpath = JNIUtil::makeJString(path);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jcopyFromPath = JNIUtil::makeJString(log_item->copyfrom_path);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jaction = EnumMapper::mapChangePathAction(log_item->action);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jnodeKind = EnumMapper::mapNodeKind(log_item->node_kind);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jlong jcopyFromRev = log_item->copyfrom_rev;

  jobject jcp = env->NewObject(clazzCP, midCP, jpath, jcopyFromRev,
                      jcopyFromPath, jaction, jnodeKind,
                      EnumMapper::mapTristate(log_item->text_modified),
                      EnumMapper::mapTristate(log_item->props_modified));
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  return env->PopLocalFrame(jcp);
}

jobject
CreateJ::Status(svn_wc_context_t *wc_ctx, const char *local_abspath,
                const svn_client_status_t *status, apr_pool_t *pool)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jclass clazz = env->FindClass(JAVA_PACKAGE"/types/Status");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static jmethodID mid = 0;
  if (mid == 0)
    {
      mid = env->GetMethodID(clazz, "<init>",
                             "(Ljava/lang/String;Ljava/lang/String;"
                             "L"JAVA_PACKAGE"/types/NodeKind;"
                             "JJJLjava/lang/String;"
                             "L"JAVA_PACKAGE"/types/Status$Kind;"
                             "L"JAVA_PACKAGE"/types/Status$Kind;"
                             "L"JAVA_PACKAGE"/types/Status$Kind;"
                             "L"JAVA_PACKAGE"/types/Status$Kind;"
                             "ZZZL"JAVA_PACKAGE"/ConflictDescriptor;"
                             "Ljava/lang/String;Ljava/lang/String;"
                             "Ljava/lang/String;Ljava/lang/String;"
                             "JZZLjava/lang/String;Ljava/lang/String;"
                             "Ljava/lang/String;"
                             "JL"JAVA_PACKAGE"/types/Lock;"
                             "JJL"JAVA_PACKAGE"/types/NodeKind;"
                             "Ljava/lang/String;Ljava/lang/String;)V");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }
  jstring jPath = JNIUtil::makeJString(local_abspath);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jUrl = NULL;
  jobject jNodeKind = NULL;
  jlong jRevision =
    org_apache_subversion_javahl_types_Revision_SVN_INVALID_REVNUM;
  jlong jLastChangedRevision =
    org_apache_subversion_javahl_types_Revision_SVN_INVALID_REVNUM;
  jlong jLastChangedDate = 0;
  jstring jLastCommitAuthor = NULL;
  jobject jTextType = NULL;
  jobject jPropType = NULL;
  jobject jRepositoryTextType = NULL;
  jobject jRepositoryPropType = NULL;
  jboolean jIsLocked = JNI_FALSE;
  jboolean jIsCopied = JNI_FALSE;
  jboolean jIsSwitched = JNI_FALSE;
  jboolean jIsFileExternal = JNI_FALSE;
  jboolean jIsTreeConflicted = JNI_FALSE;
  jobject jConflictDescription = NULL;
  jstring jConflictOld = NULL;
  jstring jConflictNew = NULL;
  jstring jConflictWorking = NULL;
  jstring jURLCopiedFrom = NULL;
  jlong jRevisionCopiedFrom =
    org_apache_subversion_javahl_types_Revision_SVN_INVALID_REVNUM;
  jstring jLockToken = NULL;
  jstring jLockComment = NULL;
  jstring jLockOwner = NULL;
  jlong jLockCreationDate = 0;
  jobject jLock = NULL;
  jlong jOODLastCmtRevision =
    org_apache_subversion_javahl_types_Revision_SVN_INVALID_REVNUM;
  jlong jOODLastCmtDate = 0;
  jobject jOODKind = NULL;
  jstring jOODLastCmtAuthor = NULL;
  jstring jChangelist = NULL;
  if (status != NULL)
    {
      /* ### Calculate the old style text_status value to make
         ### the tests pass. It is probably better to do this in
         ### the tigris package and then switch the apache package
         ### to three statuses. */
      enum svn_wc_status_kind text_status = status->node_status;

      /* Avoid using values that might come from prop changes */
      if (text_status == svn_wc_status_modified
          || text_status == svn_wc_status_conflicted)
        text_status = status->text_status;

      jTextType = EnumMapper::mapStatusKind(text_status);
      jPropType = EnumMapper::mapStatusKind(status->prop_status);
      jRepositoryTextType = EnumMapper::mapStatusKind(
                                                      status->repos_text_status);
      jRepositoryPropType = EnumMapper::mapStatusKind(
                                                      status->repos_prop_status);
      jIsCopied = (status->copied == 1) ? JNI_TRUE: JNI_FALSE;
      jIsLocked = (status->locked == 1) ? JNI_TRUE: JNI_FALSE;
      jIsSwitched = (status->switched == 1) ? JNI_TRUE: JNI_FALSE;
      jIsFileExternal = (status->file_external == 1) ? JNI_TRUE: JNI_FALSE;

      /* Unparse the meaning of the conflicted flag. */
      if (status->conflicted)
        {
          svn_boolean_t text_conflicted = FALSE;
          svn_boolean_t prop_conflicted = FALSE;
          svn_boolean_t tree_conflicted = FALSE;

          SVN_JNI_ERR(svn_wc_conflicted_p3(&text_conflicted,
                                           &prop_conflicted,
                                           &tree_conflicted,
                                           wc_ctx, local_abspath,
                                           pool),
                      NULL);

          if (tree_conflicted)
            {
              jIsTreeConflicted = JNI_TRUE;

              const svn_wc_conflict_description2_t *tree_conflict;
              SVN_JNI_ERR(svn_wc__get_tree_conflict(&tree_conflict, wc_ctx,
                                                    local_abspath, pool, pool),
                          NULL);

              svn_wc_conflict_description_t *old_tree_conflict =
                                    svn_wc__cd2_to_cd(tree_conflict, pool);
              jConflictDescription = CreateJ::ConflictDescriptor
                                                            (old_tree_conflict);
              if (JNIUtil::isJavaExceptionThrown())
                POP_AND_RETURN_NULL;
            }

          if (text_conflicted)
            {
              /* ### Fetch conflict marker files, still handled via svn_wc_entry_t */
            }

          if (prop_conflicted)
            {
              /* ### Fetch conflict marker file, still handled via svn_wc_entry_t */
            }
        }

      jLock = CreateJ::Lock(status->repos_lock);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      if (status->repos_root_url)
        {
          jUrl = JNIUtil::makeJString(svn_path_url_add_component2(
                                        status->repos_root_url,
                                        status->repos_relpath,
                                        pool));
          if (JNIUtil::isJavaExceptionThrown())
            POP_AND_RETURN_NULL;
        }

      jOODLastCmtRevision = status->ood_changed_rev;
      jOODLastCmtDate = status->ood_changed_date;
      jOODKind = EnumMapper::mapNodeKind(status->ood_kind);
      jOODLastCmtAuthor = JNIUtil::makeJString(status->ood_changed_author);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      if (status->versioned)
        {
          jNodeKind = EnumMapper::mapNodeKind(status->kind);
          jRevision = status->revision;
          jLastChangedRevision = status->changed_rev;
          jLastChangedDate = status->changed_date;
          jLastCommitAuthor = JNIUtil::makeJString(status->changed_author);

          if (JNIUtil::isJavaExceptionThrown())
            POP_AND_RETURN_NULL;

          if (status->lock)
            {
              jLockToken = JNIUtil::makeJString(status->lock->token);
              if (JNIUtil::isJavaExceptionThrown())
                POP_AND_RETURN_NULL;

              jLockComment = JNIUtil::makeJString(status->lock->comment);
              if (JNIUtil::isJavaExceptionThrown())
                POP_AND_RETURN_NULL;

              jLockOwner = JNIUtil::makeJString(status->lock->owner);
              if (JNIUtil::isJavaExceptionThrown())
                POP_AND_RETURN_NULL;

              jLockCreationDate = status->lock->creation_date;
            }

          jChangelist = JNIUtil::makeJString(status->changelist);
          if (JNIUtil::isJavaExceptionThrown())
            POP_AND_RETURN_NULL;
        }

      if (status->versioned && status->conflicted)
        {
          const char *conflict_new, *conflict_old, *conflict_wrk;
          const char *copyfrom_url;
          svn_revnum_t copyfrom_rev;
          svn_boolean_t is_copy_target;

          /* This call returns SVN_ERR_ENTRY_NOT_FOUND for some hidden
             cases, which we can just ignore here as hidden nodes
             are not in text or property conflict. */
          svn_error_t *err = svn_wc__node_get_conflict_info(&conflict_old,
                                                            &conflict_new,
                                                            &conflict_wrk,
                                                            NULL,
                                                            wc_ctx,
                                                            local_abspath,
                                                            pool, pool);

          if (err)
            {
               if (err->apr_err == SVN_ERR_ENTRY_NOT_FOUND)
                 svn_error_clear(err);
               else
                 SVN_JNI_ERR(err, NULL);
            }

          jConflictNew = JNIUtil::makeJString(conflict_new);
          if (JNIUtil::isJavaExceptionThrown())
            POP_AND_RETURN_NULL;

          jConflictOld = JNIUtil::makeJString(conflict_old);
          if (JNIUtil::isJavaExceptionThrown())
            POP_AND_RETURN_NULL;

          jConflictWorking= JNIUtil::makeJString(conflict_wrk);
          if (JNIUtil::isJavaExceptionThrown())
            POP_AND_RETURN_NULL;

          SVN_JNI_ERR(svn_wc__node_get_copyfrom_info(NULL, NULL,
                                                     &copyfrom_url,
                                                     &copyfrom_rev,
                                                     &is_copy_target,
                                                     wc_ctx, local_abspath,
                                                     pool, pool), NULL);

          jURLCopiedFrom = JNIUtil::makeJString(is_copy_target ? copyfrom_url
                                                               : NULL);
          if (JNIUtil::isJavaExceptionThrown())
            POP_AND_RETURN_NULL;

          jRevisionCopiedFrom = is_copy_target ? copyfrom_rev
                                               : SVN_INVALID_REVNUM;
        }
    }

  jobject ret = env->NewObject(clazz, mid, jPath, jUrl, jNodeKind, jRevision,
                               jLastChangedRevision, jLastChangedDate,
                               jLastCommitAuthor, jTextType, jPropType,
                               jRepositoryTextType, jRepositoryPropType,
                               jIsLocked, jIsCopied, jIsTreeConflicted,
                               jConflictDescription, jConflictOld, jConflictNew,
                               jConflictWorking, jURLCopiedFrom,
                               jRevisionCopiedFrom, jIsSwitched, jIsFileExternal,
                               jLockToken, jLockOwner,
                               jLockComment, jLockCreationDate, jLock,
                               jOODLastCmtRevision, jOODLastCmtDate,
                               jOODKind, jOODLastCmtAuthor, jChangelist);

  return env->PopLocalFrame(ret);
}

jobject
CreateJ::ClientNotifyInformation(const svn_wc_notify_t *wcNotify)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  static jmethodID midCT = 0;
  jclass clazz = env->FindClass(JAVA_PACKAGE"/ClientNotifyInformation");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  if (midCT == 0)
    {
      midCT = env->GetMethodID(clazz, "<init>",
                               "(Ljava/lang/String;"
                               "L"JAVA_PACKAGE"/ClientNotifyInformation$Action;"
                               "L"JAVA_PACKAGE"/types/NodeKind;"
                               "Ljava/lang/String;"
                               "L"JAVA_PACKAGE"/types/Lock;"
                               "Ljava/lang/String;"
                               "L"JAVA_PACKAGE"/ClientNotifyInformation$Status;"
                               "L"JAVA_PACKAGE"/ClientNotifyInformation$Status;"
                               "L"JAVA_PACKAGE"/ClientNotifyInformation$LockStatus;"
                               "JLjava/lang/String;"
                               "L"JAVA_PACKAGE"/types/RevisionRange;"
                               "Ljava/lang/String;Ljava/lang/String;"
                               "Ljava/util/Map;JJJJJJI)V");
      if (JNIUtil::isJavaExceptionThrown() || midCT == 0)
        POP_AND_RETURN_NULL;
    }

  // convert the parameter to their Java relatives
  jstring jPath = JNIUtil::makeJString(wcNotify->path);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jAction = EnumMapper::mapNotifyAction(wcNotify->action);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jKind = EnumMapper::mapNodeKind(wcNotify->kind);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jMimeType = JNIUtil::makeJString(wcNotify->mime_type);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jLock = CreateJ::Lock(wcNotify->lock);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jErr = JNIUtil::makeSVNErrorMessage(wcNotify->err);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jContentState = EnumMapper::mapNotifyState(wcNotify->content_state);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jPropState = EnumMapper::mapNotifyState(wcNotify->prop_state);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jLockState = EnumMapper::mapNotifyLockState(wcNotify->lock_state);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jChangelistName = JNIUtil::makeJString(wcNotify->changelist_name);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jMergeRange = NULL;
  if (wcNotify->merge_range)
    {
      jMergeRange = RevisionRange::makeJRevisionRange(wcNotify->merge_range);
      if (jMergeRange == NULL)
        POP_AND_RETURN_NULL;
    }

  jstring jpathPrefix = JNIUtil::makeJString(wcNotify->path_prefix);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jpropName = JNIUtil::makeJString(wcNotify->prop_name);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jobject jrevProps = CreateJ::PropertyMap(wcNotify->rev_props);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jlong joldRevision = wcNotify->old_revision;
  jlong jhunkOriginalStart = wcNotify->hunk_original_start;
  jlong jhunkOriginalLength = wcNotify->hunk_original_length;
  jlong jhunkModifiedStart = wcNotify->hunk_modified_start;
  jlong jhunkModifiedLength = wcNotify->hunk_modified_length;
  jlong jhunkMatchedLine = wcNotify->hunk_matched_line;
  jint jhunkFuzz = wcNotify->hunk_fuzz;

  // call the Java method
  jobject jInfo = env->NewObject(clazz, midCT, jPath, jAction,
                                 jKind, jMimeType, jLock, jErr,
                                 jContentState, jPropState, jLockState,
                                 (jlong) wcNotify->revision, jChangelistName,
                                 jMergeRange, jpathPrefix, jpropName,
                                 jrevProps, joldRevision,
                                 jhunkOriginalStart, jhunkOriginalLength,
                                 jhunkModifiedStart, jhunkModifiedLength,
                                 jhunkMatchedLine, jhunkFuzz);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  return env->PopLocalFrame(jInfo);
}

jobject
CreateJ::ReposNotifyInformation(const svn_repos_notify_t *reposNotify)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  static jmethodID midCT = 0;
  jclass clazz = env->FindClass(JAVA_PACKAGE"/ReposNotifyInformation");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  if (midCT == 0)
    {
      midCT = env->GetMethodID(clazz, "<init>",
                               "(L"JAVA_PACKAGE"/ReposNotifyInformation$Action;"
                               "JLjava/lang/String;JJJ"
                               "L"JAVA_PACKAGE"/ReposNotifyInformation$NodeAction;"
                               "Ljava/lang/String;)V");
      if (JNIUtil::isJavaExceptionThrown() || midCT == 0)
        POP_AND_RETURN_NULL;
    }

  // convert the parameters to their Java relatives
  jobject jAction = EnumMapper::mapReposNotifyAction(reposNotify->action);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jWarning = JNIUtil::makeJString(reposNotify->warning);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jlong jRevision = (jlong)reposNotify->revision;
  jlong jShard = (jlong)reposNotify->shard;
  jlong jnewRevision = (jlong)reposNotify->new_revision;
  jlong joldRevision = (jlong)reposNotify->old_revision;

  jobject jnodeAction = EnumMapper::mapReposNotifyNodeAction(
                                                    reposNotify->node_action);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jpath = JNIUtil::makeJString(reposNotify->path);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  // call the Java method
  jobject jInfo = env->NewObject(clazz, midCT, jAction, jRevision, jWarning,
                                 jShard, jnewRevision, joldRevision,
                                 jnodeAction, jpath);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  return env->PopLocalFrame(jInfo);
}

jobject
CreateJ::CommitItem(svn_client_commit_item3_t *item)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jclass clazz = env->FindClass(JAVA_PACKAGE"/CommitItem");
  if (JNIUtil::isExceptionThrown())
    POP_AND_RETURN_NULL;

  // Get the method id for the CommitItem constructor.
  static jmethodID midConstructor = 0;
  if (midConstructor == 0)
    {
      midConstructor = env->GetMethodID(clazz, "<init>",
                                        "(Ljava/lang/String;"
                                        "L"JAVA_PACKAGE"/types/NodeKind;"
                                        "ILjava/lang/String;"
                                        "Ljava/lang/String;J)V");
      if (JNIUtil::isExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  jstring jpath = JNIUtil::makeJString(item->path);

  jobject jnodeKind = EnumMapper::mapNodeKind(item->kind);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jint jstateFlags = 0;
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
    jstateFlags |=
      org_apache_subversion_javahl_CommitItemStateFlags_Add;
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
    jstateFlags |=
      org_apache_subversion_javahl_CommitItemStateFlags_Delete;
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_TEXT_MODS)
    jstateFlags |=
      org_apache_subversion_javahl_CommitItemStateFlags_TextMods;
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_PROP_MODS)
    jstateFlags |=
      org_apache_subversion_javahl_CommitItemStateFlags_PropMods;
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_IS_COPY)
    jstateFlags |=
      org_apache_subversion_javahl_CommitItemStateFlags_IsCopy;

  jstring jurl = JNIUtil::makeJString(item->url);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jcopyUrl = JNIUtil::makeJString(item->copyfrom_url);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jlong jcopyRevision = item->revision;

  // create the Java object
  jobject jitem = env->NewObject(clazz, midConstructor, jpath,
                                 jnodeKind, jstateFlags, jurl,
                                 jcopyUrl, jcopyRevision);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  return env->PopLocalFrame(jitem);
}

jobject
CreateJ::CommitInfo(const svn_commit_info_t *commit_info)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  static jmethodID midCT = 0;
  jclass clazz = env->FindClass(JAVA_PACKAGE"/CommitInfo");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  if (midCT == 0)
    {
      midCT = env->GetMethodID(clazz, "<init>",
                               "(JLjava/lang/String;Ljava/lang/String;)V");
      if (JNIUtil::isJavaExceptionThrown() || midCT == 0)
        POP_AND_RETURN_NULL;
    }

  jstring jAuthor = JNIUtil::makeJString(commit_info->author);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jstring jDate = JNIUtil::makeJString(commit_info->date);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  jlong jRevision = commit_info->revision;

  // call the Java method
  jobject jInfo = env->NewObject(clazz, midCT, jRevision, jDate, jAuthor);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  return env->PopLocalFrame(jInfo);
}

jobject
CreateJ::RevisionRangeList(apr_array_header_t *ranges)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jclass clazz = env->FindClass("java/util/ArrayList");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static jmethodID init_mid = 0;
  if (init_mid == 0)
    {
      init_mid = env->GetMethodID(clazz, "<init>", "()V");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  static jmethodID add_mid = 0;
  if (add_mid == 0)
    {
      add_mid = env->GetMethodID(clazz, "add", "(Ljava/lang/Object;)Z");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  jobject jranges = env->NewObject(clazz, init_mid);

  for (int i = 0; i < ranges->nelts; ++i)
    {
      // Convert svn_merge_range_t *'s to Java RevisionRange objects.
      svn_merge_range_t *range =
          APR_ARRAY_IDX(ranges, i, svn_merge_range_t *);

      jobject jrange = RevisionRange::makeJRevisionRange(range);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      env->CallBooleanMethod(jranges, add_mid, jrange);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      env->DeleteLocalRef(jrange);
    }

  return env->PopLocalFrame(jranges);
}

jobject
CreateJ::StringSet(apr_array_header_t *strings)
{
  std::vector<jobject> jstrs;

  for (int i = 0; i < strings->nelts; ++i)
    {
      const char *str = APR_ARRAY_IDX(strings, i, const char *);
      jstring jstr = JNIUtil::makeJString(str);
      if (JNIUtil::isJavaExceptionThrown())
        return NULL;

      jstrs.push_back(jstr);
    }

  return CreateJ::Set(jstrs);
}

jobject CreateJ::PropertyMap(apr_hash_t *prop_hash)
{
  JNIEnv *env = JNIUtil::getEnv();

  if (prop_hash == NULL)
    return NULL;

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jclass clazz = env->FindClass("java/util/HashMap");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static jmethodID init_mid = 0;
  if (init_mid == 0)
    {
      init_mid = env->GetMethodID(clazz, "<init>", "()V");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  static jmethodID put_mid = 0;
  if (put_mid == 0)
    {
      put_mid = env->GetMethodID(clazz, "put",
                                 "(Ljava/lang/Object;Ljava/lang/Object;)"
                                 "Ljava/lang/Object;");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  jobject map = env->NewObject(clazz, init_mid);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  apr_hash_index_t *hi;
  int i = 0;
  for (hi = apr_hash_first(apr_hash_pool_get(prop_hash), prop_hash);
       hi; hi = apr_hash_next(hi), ++i)
    {
      const char *key;
      svn_string_t *val;

      apr_hash_this(hi, (const void **)&key, NULL, (void **)&val);

      jstring jpropName = JNIUtil::makeJString(key);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      jbyteArray jpropVal = JNIUtil::makeJByteArray(
                                    (const signed char *)val->data, val->len);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      env->CallObjectMethod(map, put_mid, jpropName, jpropVal);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      env->DeleteLocalRef(jpropName);
      env->DeleteLocalRef(jpropVal);
    }

  return env->PopLocalFrame(map);
}

jobject CreateJ::Set(std::vector<jobject> &objects)
{
  JNIEnv *env = JNIUtil::getEnv();

  // Create a local frame for our references
  env->PushLocalFrame(LOCAL_FRAME_SIZE);
  if (JNIUtil::isJavaExceptionThrown())
    return NULL;

  jclass clazz = env->FindClass("java/util/HashSet");
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  static jmethodID init_mid = 0;
  if (init_mid == 0)
    {
      init_mid = env->GetMethodID(clazz, "<init>", "()V");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  static jmethodID add_mid = 0;
  if (add_mid == 0)
    {
      add_mid = env->GetMethodID(clazz, "add", "(Ljava/lang/Object;)Z");
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;
    }

  jobject set = env->NewObject(clazz, init_mid);
  if (JNIUtil::isJavaExceptionThrown())
    POP_AND_RETURN_NULL;

  std::vector<jobject>::const_iterator it;
  for (it = objects.begin(); it < objects.end(); ++it)
    {
      jobject jthing = *it;

      env->CallBooleanMethod(set, add_mid, jthing);
      if (JNIUtil::isJavaExceptionThrown())
        POP_AND_RETURN_NULL;

      env->DeleteLocalRef(jthing);
    }

  return env->PopLocalFrame(set);
}
