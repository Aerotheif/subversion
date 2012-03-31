/*
 * crypto.h :  cryptographic routines
 *
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
 */

#ifndef SVN_LIBSVN_SUBR_CRYPTO_H
#define SVN_LIBSVN_SUBR_CRYPTO_H

#include <apu.h>  /* for APU_HAVE_CRYPTO */

#if APU_HAVE_CRYPTO

#include "svn_types.h"
#include "svn_string.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Opaque context for cryptographic operations.  */
typedef struct svn_crypto__ctx_t svn_crypto__ctx_t;


/* Set *CTX to new Subversion cryptographic context, based on an
   APR-managed OpenSSL cryptography context object allocated
   within RESULT_POOL.  */
/* ### TODO: Should this be something done once with the resulting
   ### svn_crypto__ctx_t object stored in svn_client_ctx_t?  */
svn_error_t *
svn_crypto__context_create(svn_crypto__ctx_t **ctx,
                           apr_pool_t *result_pool);


/* Using a PBKDF2 derivative key based on MASTER, encrypt PLAINTEXT.
   The salt used for PBKDF2 is returned in SALT, and the IV used for
   the (AES-256/CBC) encryption is returned in IV. The resulting
   encrypted data is returned in CIPHERTEXT.

   Note that MASTER may be the plaintext obtained from the user or
   some other OS-provided cryptographic store, or it can be a derivation
   such as SHA1(plaintext). As long as the same octets are passed to
   the decryption function, everything works just fine. (the SHA1
   approach is suggested, to avoid keeping the plaintext master in
   the process' memory space)  */
svn_error_t *
svn_crypto__encrypt_password(const svn_string_t **ciphertext,
                             const svn_string_t **iv,
                             const svn_string_t **salt,
                             svn_crypto__ctx_t *ctx,
                             const char *plaintext,
                             const svn_string_t *master,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);


/* Given the CIPHERTEXT which was encrypted using (AES-256/CBC) with
   initialization vector given by IV, and a key derived using PBKDF2
   with SALT and MASTER... return the decrypted password in PLAINTEXT.  */
svn_error_t *
svn_crypto__decrypt_password(const char **plaintext,
                             svn_crypto__ctx_t *ctx,
                             const svn_string_t *ciphertext,
                             const svn_string_t *iv,
                             const svn_string_t *salt,
                             const svn_string_t *master,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* APU_HAVE_CRYPTO */

#endif  /* SVN_CRYPTO_H */
