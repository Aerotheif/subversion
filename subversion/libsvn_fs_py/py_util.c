/* py_util.c : some help for the embedded python interpreter
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

#include <apr_pools.h>

#include "svn_error.h"
#include "svn_pools.h"

#include "py_util.h"

#include "svn_private_config.h"

#define ROOT_MODULE_NAME "svn"
#define FS_MODULE_NAME "svn.fs"

static svn_error_t *
create_py_stack(PyObject *p_exception,
                PyObject *p_traceback)
{
  svn_error_t *err;
  PyObject *p_reason;
  char *reason;

  p_reason = PyObject_Str(p_exception);
  reason = PyString_AsString(p_reason);

  err = svn_error_createf(SVN_ERR_BAD_PYTHON, NULL,
                          _("Exception while executing Python; cause: \"%s\""),
                          reason);

#ifdef SVN_ERR__TRACING
  if (p_traceback)
    {
      PyObject *p_module_name;
      PyObject *p_traceback_mod;
      PyObject *p_stack;
      PyObject *p_frame;
      PyObject *p_filename;
      PyObject *p_lineno;
      Py_ssize_t i;

      /* We don't use load_module() here to avoid an infinite recursion. */
      /* ### This could use more python error checking. */
      p_module_name = PyString_FromString("traceback");
      p_traceback_mod = PyImport_Import(p_module_name);
      Py_DECREF(p_module_name);

      /* ### Cast away const for the Python API. */
      p_stack = PyObject_CallMethod(p_traceback_mod, (char *) "extract_tb",
                                    (char *) "(O)", p_traceback);
      Py_DECREF(p_traceback_mod);

      i = PySequence_Length(p_stack);

      /* Build the "root error" for the chain. */
      p_frame = PySequence_GetItem(p_stack, i-1);
      p_filename = PySequence_GetItem(p_frame, 0);
      p_lineno = PySequence_GetItem(p_frame, 1);
      Py_DECREF(p_frame);

      err->file = apr_pstrdup(err->pool, PyString_AsString(p_filename));
      err->line = PyInt_AsLong(p_lineno);

      Py_DECREF(p_filename);
      Py_DECREF(p_lineno);

      for (i = i-2; i >=0; i--)
        {
          p_frame = PySequence_GetItem(p_stack, i);
          p_filename = PySequence_GetItem(p_frame, 0);
          p_lineno = PySequence_GetItem(p_frame, 1);
          Py_DECREF(p_frame);

          err = svn_error_quick_wrap(err, SVN_ERR__TRACED);
          err->file = apr_pstrdup(err->pool, PyString_AsString(p_filename));
          err->line = PyInt_AsLong(p_lineno);

          Py_DECREF(p_filename);
          Py_DECREF(p_lineno);
        }

      Py_DECREF(p_stack);
    }
#endif

  /* If the exception object has a 'code' attribute, and it's an integer,
     assume it's an apr_err code */
  if (PyObject_HasAttrString(p_exception, "code"))
    {
      PyObject *p_code = PyObject_GetAttrString(p_exception, "code");

      if (PyInt_Check(p_code))
        err->apr_err = (int) PyInt_AS_LONG(p_code);

      Py_DECREF(p_code);
    }

  Py_DECREF(p_reason);

  return svn_error_trace(err);
}

typedef void (*py_exc_func_t)(void *baton, va_list argp);

/* Call FUNC with BATON, and upon returning check to see if the Python
   interpreter has a pending exception.  If it does, convert that exception
   to an svn_error_t and return it (or SVN_NO_ERROR if no error), resetting
   the interpreter state and releasing the exception.

   Note: This function assumes whatever locking we need for the interpreter
   has already happened and will be released after it is done. */
static svn_error_t *
catch_py_exception(py_exc_func_t func,
                   void *baton,
                   va_list argp)
{
  PyObject *p_type;
  PyObject *p_exception;
  PyObject *p_traceback;
  svn_error_t *err;

  /* Call the handler. */
  func(baton, argp);

  /* Early out if we didn't have any errors. */
  if (!PyErr_Occurred())
    return SVN_NO_ERROR;

  PyErr_Fetch(&p_type, &p_exception, &p_traceback);

  if (p_exception)
    err = create_py_stack(p_exception, p_traceback);
  else
    err = svn_error_create(SVN_ERR_BAD_PYTHON, NULL,
                           _("Python error."));

  PyErr_Clear();

  Py_DECREF(p_type);
  Py_XDECREF(p_exception);
  Py_XDECREF(p_traceback);

  return svn_error_trace(err);
}

static svn_error_t *
load_module(PyObject **p_module_out,
            const char *module_name)
{
  PyObject *p_module_name = NULL;
  PyObject *p_module = NULL;

  p_module_name = PyString_FromString(module_name);
  if (PyErr_Occurred())
    goto load_error;

  p_module = PyImport_Import(p_module_name);
  Py_DECREF(p_module_name);
  if (PyErr_Occurred())
    goto load_error;

  *p_module_out = p_module;

  return SVN_NO_ERROR;

load_error:
  {
    PyObject *p_type, *p_exception, *p_traceback;
    svn_error_t *err;

    /* Release the values above. */
    Py_XDECREF(p_module);
    *p_module_out = NULL;

    PyErr_Fetch(&p_type, &p_exception, &p_traceback);

    if (p_exception && p_traceback)
      err = create_py_stack(p_exception, p_traceback);
    else
      err = svn_error_create(SVN_ERR_BAD_PYTHON, NULL,
                             _("Cannot load Python module"));

    PyErr_Clear();

    Py_DECREF(p_type);
    Py_XDECREF(p_exception);
    Py_XDECREF(p_traceback);

    return err;
  }
}

svn_error_t *
svn_fs_py__init_python(apr_pool_t *pool)
{
  if (Py_IsInitialized())
    return SVN_NO_ERROR;

  Py_SetProgramName((char *) "svn");
  Py_InitializeEx(0);

  /* Note: we don't have a matching call to Py_Finalize() because we don't
     know if we initialize python, or if we are in an environment where
     finalizing Python would interact with interpreters which are didn't
     create.  The interpreter state isn't very large (1-2MB), so we essentially
     just leak it. */

  return SVN_NO_ERROR;
}


apr_status_t
svn_fs_py__destroy_py_object(void *data)
{
  PyObject *p_fs = data;
  Py_XDECREF(p_fs);

  return APR_SUCCESS;
}


struct get_string_attr_baton
{
  const char **result;
  PyObject *p_obj;
  const char *name;
  apr_pool_t *result_pool;
};


static void
get_string_attr(void *baton,
                va_list argp)
{
  struct get_string_attr_baton *gsab = baton;
  PyObject *p_attr;
  PyObject *p_str;

  /* ### This needs some exception handling */

  p_attr = PyObject_GetAttrString(gsab->p_obj, gsab->name);
  if (PyErr_Occurred())
    return;

  p_str = PyObject_Str(p_attr);
  Py_DECREF(p_attr);
  if (PyErr_Occurred())
    return;

  *gsab->result = PyString_AsString(p_str);

  if (gsab->result)
    *gsab->result = apr_pstrdup(gsab->result_pool, *gsab->result);

  Py_DECREF(p_str);

  return;
}


svn_error_t *
svn_fs_py__get_string_attr(const char **result,
                           PyObject *p_obj,
                           const char *name,
                           apr_pool_t *result_pool)
{
  struct get_string_attr_baton gsab = {
      result, p_obj, name, result_pool
    };
  return svn_error_trace(catch_py_exception(get_string_attr, &gsab, NULL));
}



struct get_int_attr_baton
{
  int *result;
  PyObject *p_obj;
  const char *name;
};


static void
get_int_attr(void *baton,
             va_list argp)
{
  struct get_int_attr_baton *giab = baton;
  PyObject *p_int;

  /* ### This needs some exception handling */

  p_int = PyObject_GetAttrString(giab->p_obj, giab->name);
  if (PyErr_Occurred())
    return;

  *giab->result = (int) PyInt_AsLong(p_int);
  Py_DECREF(p_int);

  return;
}


svn_error_t *
svn_fs_py__get_int_attr(int *result,
                        PyObject *p_obj,
                        const char *name)
{
  struct get_int_attr_baton giab = { result, p_obj, name };
  return svn_error_trace(catch_py_exception(get_int_attr, &giab, NULL));
}


struct set_int_attr_baton
{
  PyObject *p_obj;
  const char *name;
  long int val;
};


static void
set_int_attr(void *baton,
             va_list argp)
{
  struct set_int_attr_baton *siab = baton;
  PyObject *p_int;

  p_int = PyInt_FromLong(siab->val);
  if (PyErr_Occurred())
    return;

  PyObject_SetAttrString(siab->p_obj, siab->name, p_int);
  Py_DECREF(p_int);

  return;
}


svn_error_t *
svn_fs_py__set_int_attr(PyObject *p_obj,
                        const char *name,
                        long int val)
{
  struct set_int_attr_baton siab = { p_obj, name, val };
  return svn_error_trace(catch_py_exception(set_int_attr, &siab, NULL));
}


struct call_method_baton
{
  PyObject **p_result;
  PyObject *p_obj;
  const char *name;
  const char *format;
};


static void
call_method(void *baton, va_list argp)
{
  struct call_method_baton *cmb = baton;
  PyObject *p_args = NULL;
  PyObject *p_func = NULL;
  PyObject *p_value = NULL;

  p_args = Py_VaBuildValue(cmb->format, argp);
  if (PyErr_Occurred())
    goto cm_free_objs;

  p_func = PyObject_GetAttrString(cmb->p_obj, cmb->name);
  if (PyErr_Occurred())
    goto cm_free_objs;

  p_value = PyObject_CallObject(p_func, p_args);
  Py_DECREF(p_args);
  p_args = NULL;
  Py_DECREF(p_func);
  p_func = NULL;
  if (PyErr_Occurred())
    goto cm_free_objs;

  if (cmb->p_result)
    *cmb->p_result = p_value;
  else
    Py_DECREF(p_value);

  return;

cm_free_objs:
  /* Error handler, decrefs all python objects we may have. */
  Py_XDECREF(p_args);
  Py_XDECREF(p_func);
  Py_XDECREF(p_value);
}


svn_error_t*
svn_fs_py__call_method(PyObject **p_result,
                       PyObject *p_obj,
                       const char *name,
                       const char *format,
                       ...)
{
  svn_error_t *err;
  va_list argp;
  struct call_method_baton cmb = {
      p_result, p_obj, name, format
    };

  SVN_ERR_ASSERT(p_obj != NULL);

  va_start(argp, format);
  err = catch_py_exception(call_method, &cmb, argp);
  va_end(argp);

  return svn_error_trace(err);
}


static PyObject *
convert_hash(apr_hash_t *hash,
             PyObject *(*converter_func)(void *value))
{
  apr_hash_index_t *hi;
  PyObject *dict;

  if (hash == NULL)
    Py_RETURN_NONE;

  if ((dict = PyDict_New()) == NULL)
    return NULL;

  for (hi = apr_hash_first(NULL, hash); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      PyObject *value;

      apr_hash_this(hi, &key, NULL, &val);
      value = (*converter_func)(val);
      if (value == NULL)
        {
          Py_DECREF(dict);
          return NULL;
        }
      /* ### gotta cast this thing cuz Python doesn't use "const" */
      if (PyDict_SetItemString(dict, (char *)key, value) == -1)
        {
          Py_DECREF(value);
          Py_DECREF(dict);
          return NULL;
        }
      Py_DECREF(value);
    }

  return dict;
}

static PyObject *
convert_svn_string_t(void *value)
{
  const svn_string_t *s = value;

  /* ### gotta cast this thing cuz Python doesn't use "const" */
  return PyString_FromStringAndSize((void *)s->data, s->len);
}

PyObject *
svn_fs_py__convert_cstring_hash(void *object)
{
  /* Cast PyString_FromString to silence a compiler warning. */
  return convert_hash(object, (PyObject *(*)(void *value)) PyString_FromString);
}

PyObject *
svn_fs_py__convert_proplist(void *object)
{
  return convert_hash(object, convert_svn_string_t);
}

svn_error_t *
svn_fs_py__load_module(fs_fs_data_t *ffd)
{
  return svn_error_trace(load_module(&ffd->p_module, FS_MODULE_NAME));
}
