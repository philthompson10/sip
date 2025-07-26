/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file implements the API for the argument parsers support.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#include "sip_parsers.h"

#include "sip_array.h"
#include "sip_container.h"
#include "sip_core.h"
#include "sip_enum.h"
#include "sip_int_convertors.h"
#include "sip_module.h"
#include "sip_object_map.h"
#include "sip_string_convertors.h"
#include "sip_voidptr.h"
#include "sip_wrapper.h"
#include "sip_wrapper_type.h"


/*
 * These are the format flags supported by argument parsers.
 */
#define FMT_AP_DEREF            0x01    /* The pointer will be dereferenced. */
#define FMT_AP_TRANSFER         0x02    /* Implement /Transfer/. */
#define FMT_AP_TRANSFER_BACK    0x04    /* Implement /TransferBack/. */
#define FMT_AP_NO_CONVERTORS    0x08    /* Suppress any convertors. */
#define FMT_AP_TRANSFER_THIS    0x10    /* Support for /TransferThis/. */


/*
 * These are the format flags supported by result parsers.  Deprecated values
 * have a _DEPR suffix.
 */
#define FMT_RP_DEREF            0x01    /* The pointer will be dereferenced. */
#define FMT_RP_FACTORY          0x02    /* /Factory/ or /TransferBack/. */
#define FMT_RP_MAKE_COPY        0x04    /* Return a copy of the value. */
#define FMT_RP_NO_STATE_DEPR    0x04    /* Don't return the C/C++ state. */


/*
 * The different reasons for failing to parse an overload.  These include
 * internal (i.e. non-user) errors.
 */
typedef enum {
    Ok, Unbound, TooFew, TooMany, UnknownKeyword, Duplicate, WrongType, Raised,
    KeywordNotString, Exception, Overflow
} sipParseFailureReason;


/*
 * The description of a failure to parse an overload because of a user error.
 */
typedef struct {
    sipParseFailureReason reason;   /* The reason for the failure. */
    const char *detail_str;         /* The detail if a string. */
    PyObject *detail_obj;           /* The detail if a Python object. */
    int arg_nr;                     /* The wrong positional argument. */
    const char *arg_name;           /* The wrong keyword argument. */
    int overflow_arg_nr;            /* The overflowed positional argument. */
    const char *overflow_arg_name;  /* The overflowed keyword argument. */
} sipParseFailure;


/* Forward references. */
static void add_failure(PyObject **parse_err_p, sipParseFailure *failure);
static PyObject *bad_type_str(int arg_nr, PyObject *arg);
static PyObject *build_object(sipWrappedModuleState *wms, PyObject *tup,
        const char *fmt, va_list va);
static PyObject *call_method(sipWrappedModuleState *wms, PyObject *method,
        const char *fmt, va_list va);
static int can_convert_from_sequence(sipWrappedModuleState *wms, PyObject *seq,
        sipTypeID type_id);
static int can_convert_to_type(sipWrappedModuleState *wms, PyObject *pyObj,
        sipTypeID type_id, int flags);
static int check_encoded_string(PyObject *obj);
static PyObject *convert_from_new_type(sipWrappedModuleState *wms, void *cpp,
        sipTypeID type_id, PyObject *transferObj);
static int convert_from_sequence(sipWrappedModuleState *wms, PyObject *seq,
        sipTypeID type_id, void **array, Py_ssize_t *nr_elem);
static PyTypeObject *convert_subclass(sipSipModuleState *sms,
        PyTypeObject *py_type, const sipTypeDef **td_p, void **cppPtr_p);
static int convert_subclass_pass(sipSipModuleState *sms,
        PyTypeObject **py_type_p, const sipTypeDef **td_p, void **cppPtr_p);
static PyObject *convert_to_sequence(sipWrappedModuleState *wms, void *array,
        Py_ssize_t nr_elem, sipTypeID type_id);
static void *convert_to_type_us(sipWrappedModuleState *wms, PyObject *pyObj,
        sipTypeID type_id, PyObject *transferObj, int flags, int *statep,
        void **user_statep, int *iserrp);
static sipSimpleWrapper *deref_mixin(sipSimpleWrapper *w);
static PyObject *detail_from_failure(PyObject *failure_obj);
static void failure_dtor(PyObject *capsule);
static PyObject *get_kwd_arg(PyObject *const *args, Py_ssize_t nr_args,
        PyObject *kwd_names, Py_ssize_t nr_kwd_names, const char *name);
static PyObject *get_pyobject(sipSipModuleState *sms, void *cppPtr,
        PyTypeObject *py_type, const sipTypeDef *td);
static int get_self_from_args(PyTypeObject *py_type, PyObject *const *args,
        Py_ssize_t nr_args, Py_ssize_t arg_nr, PyObject **self_p);
static void handle_failed_int_conversion(sipParseFailure *pf, PyObject *arg);
static void handle_failed_type_conversion(sipParseFailure *pf, PyObject *arg);
static int parse_kwd_args(PyObject *wmod, PyObject **parse_err_p,
        PyObject *const *args, Py_ssize_t nr_args, PyObject *kwd_names,
        const char **kwd_list, PyObject **unused, const char *fmt,
        va_list va_orig);
static int parse_pass_1(sipWrappedModuleState *wms, PyObject **parse_err_p,
        PyObject **self_p, int *self_in_args_p, PyObject *const *args,
        Py_ssize_t nr_args, PyObject *kwd_names, Py_ssize_t nr_kwd_names,
        const char **kwd_list, PyObject **unused, const char *fmt, va_list va);
static int parse_pass_2(sipWrappedModuleState *wms, PyObject *self,
        int self_in_args, PyObject *const *args, Py_ssize_t nr_args,
        PyObject *kwd_names, Py_ssize_t nr_kwd_names, const char **kwd_list,
        const char *fmt, va_list va);
static int parse_result(sipWrappedModuleState *wms, PyObject *method,
        PyObject *res, sipSimpleWrapper *py_self, const char *fmt, va_list va);
static void raise_no_convert_to(PyObject *py, const sipTypeDef *td);
static void release_type_us(sipWrappedModuleState *wms, void *cpp,
        sipTypeID type_id, int state, void *user_state);
static PyObject *signature_from_docstring(const char *doc, Py_ssize_t line);
static int user_state_is_valid(const sipTypeDef *td, void **user_statep);


/*
 * Adds the current exception to the current list of exceptions (if it is a
 * user exception) or replace the current list of exceptions.
 */
void sip_api_add_exception(sipErrorState es, PyObject **parse_err_p)
{
    assert(*parse_err_p == NULL);

    if (es == sipErrorContinue)
    {
        sipParseFailure failure;
        PyObject *e_type, *e_traceback;

        /* Get the value of the exception. */
        PyErr_Fetch(&e_type, &failure.detail_obj, &e_traceback);
        Py_XDECREF(e_type);
        Py_XDECREF(e_traceback);

        failure.reason = Exception;

        add_failure(parse_err_p, &failure);

        if (failure.reason == Raised)
        {
            Py_XDECREF(failure.detail_obj);
            es = sipErrorFail;
        }
    }

    if (es == sipErrorFail)
    {
        Py_XDECREF(*parse_err_p);
        *parse_err_p = Py_None;
        Py_INCREF(Py_None);
    }
}


/*
 * Adds a failure about an argument with an incorrect type to the current list
 * of exceptions.
 */
sipErrorState sip_api_bad_callable_arg(int arg_nr, PyObject *arg)
{
    PyObject *detail = bad_type_str(arg_nr + 1, arg);

    if (detail == NULL)
        return sipErrorFail;

    PyErr_SetObject(PyExc_TypeError, detail);
    Py_DECREF(detail);

    return sipErrorContinue;
}


/*
 * Report a Python member function with an unexpected result.
 */
void sip_api_bad_catcher_result(PyObject *method)
{
    PyObject *mname, *etype, *evalue, *etraceback;

    /*
     * Get the current exception object if there is one.  Its string
     * representation will be used as the detail of a new exception.
     */
    PyErr_Fetch(&etype, &evalue, &etraceback);
    PyErr_NormalizeException(&etype, &evalue, &etraceback);
    Py_XDECREF(etraceback);

    /*
     * This is part of the public API so we make no assumptions about the
     * method object.
     */
    if (!PyMethod_Check(method) ||
        PyMethod_GET_FUNCTION(method) == NULL ||
        !PyFunction_Check(PyMethod_GET_FUNCTION(method)) ||
        PyMethod_GET_SELF(method) == NULL)
    {
        PyErr_Format(PyExc_TypeError,
                "invalid argument to sipBadCatcherResult()");
        return;
    }

    mname = ((PyFunctionObject *)PyMethod_GET_FUNCTION(method))->func_name;

    if (evalue != NULL)
    {
        PyErr_Format(etype, "invalid result from %s.%U(), %S",
                Py_TYPE(PyMethod_GET_SELF(method))->tp_name, mname, evalue);
        Py_DECREF(evalue);
    }
    else
    {
        PyErr_Format(PyExc_TypeError, "invalid result from %s.%U()",
                Py_TYPE(PyMethod_GET_SELF(method))->tp_name, mname);
    }

    Py_XDECREF(etype);
}


/*
 * Build a result object based on a format string.
 */
PyObject *sip_api_build_result(PyObject *wmod, int *is_err_p, const char *fmt,
        ...)
{
    va_list va;

    va_start(va,fmt);

    /* Basic validation of the format string. */

    int badfmt = FALSE;
    PyObject *res = NULL;
    Py_ssize_t tupsz;

    if (*fmt == '(')
    {
        char *ep;

        if ((ep = strchr(fmt,')')) == NULL || ep[1] != '\0')
            badfmt = TRUE;
        else
            tupsz = (Py_ssize_t)(ep - fmt - 1);
    }
    else if (strlen(fmt) == 1)
    {
        tupsz = -1;
    }
    else
    {
        badfmt = TRUE;
    }

    if (badfmt)
    {
        PyErr_Format(PyExc_SystemError,
                "sipBuildResult(): invalid format string \"%s\"",fmt);
    }
    else if (tupsz < 0 || (res = PyTuple_New(tupsz)) != NULL)
    {
        sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
                wmod);

        res = build_object(wms, res, fmt, va);
    }

    va_end(va);

    if (res == NULL && is_err_p != NULL)
        *is_err_p = TRUE;

    return res;
}


/*
 * Call a virtual error handler.  This is called with the GIL and from the
 * thread that raised the error.
 */
void sip_api_call_error_handler(sipVirtErrorHandlerFunc error_handler,
        sipSimpleWrapper *py_self, sip_gilstate_t sipGILState)
{
    if (error_handler != NULL)
        error_handler(deref_mixin(py_self), sipGILState);
    else
        PyErr_Print();
}


/*
 * Call the Python re-implementation of a C++ virtual.
 */
PyObject *sip_api_call_method(PyObject *wmod, int *is_err_p, PyObject *method,
        const char *fmt, ...)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    PyObject *res;
    va_list va;

    va_start(va, fmt);
    res = call_method(wms, method, fmt, va);
    va_end(va);

    if (res == NULL && is_err_p != NULL)
        *is_err_p = TRUE;

    return res;
}


/*
 * Call the Python re-implementation of a C++ virtual that does not return a
 * value and handle the result..
 */
void sip_api_call_procedure_method(PyObject *wmod, sip_gilstate_t gil_state,
        sipVirtErrorHandlerFunc error_handler, sipSimpleWrapper *py_self,
        PyObject *method, const char *fmt, ...)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);
    va_list va;

    va_start(va, fmt);
    PyObject *res = call_method(wms, method, fmt, va);
    va_end(va);

    if (res != NULL)
    {
        Py_DECREF(res);

        if (res != Py_None)
        {
            sip_api_bad_catcher_result(method);
            res = NULL;
        }
    }

    Py_DECREF(method);

    if (res == NULL)
        sip_api_call_error_handler(error_handler, py_self, gil_state);

    SIP_RELEASE_GIL(gil_state);
}


/*
 * Check to see if a Python object can be converted to a type.
 */
int sip_api_can_convert_to_type(PyObject *wmod, PyObject *pyObj,
        sipTypeID type_id, int flags)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    return can_convert_to_type(wms, pyObj, type_id, flags);
}


/*
 * Convert a new C/C++ instance to a Python instance of a specific Python type.
 */
PyObject *sip_api_convert_from_new_pytype(PyObject *wmod, void *cpp,
        PyTypeObject *py_type, sipWrapper *owner, sipSimpleWrapper **self_p,
        const char *fmt, ...)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    PyObject *args, *res;
    va_list va;

    va_start(va, fmt);

    if ((args = PyTuple_New(strlen(fmt))) != NULL && build_object(wms, args, fmt, va) != NULL)
    {
        res = sip_wrap_instance(wms->sip_module_state, cpp, py_type, args,
                owner, (self_p != NULL ? SIP_DERIVED_CLASS : 0));

        /* Initialise the rest of an instance of a derived class. */
        if (self_p != NULL)
            *self_p = (sipSimpleWrapper *)res;
    }
    else
    {
        res = NULL;
    }

    Py_XDECREF(args);

    va_end(va);

    return res;
}


/*
 * Convert a new C/C++ instance to a Python instance.
 */
PyObject *sip_api_convert_from_new_type(PyObject *wmod, void *cpp,
        sipTypeID type_id, PyObject *transferObj)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    return convert_from_new_type(wms, cpp, type_id, transferObj);
}


/*
 * Convert a C/C++ instance to a Python instance.
 */
PyObject *sip_api_convert_from_type(PyObject *wmod, void *cpp,
        sipTypeID type_id, PyObject *transferObj)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    return sip_convert_from_type(wms, cpp, type_id, transferObj);
}


/*
 * sip_api_convert_to_type_us() without user state support.
 */
void *sip_api_convert_to_type(PyObject *wmod, PyObject *pyObj,
        sipTypeID type_id, PyObject *transferObj, int flags, int *statep,
        int *iserrp)
{
    return sip_api_convert_to_type_us(wmod, pyObj, type_id, transferObj, flags,
            statep, NULL, iserrp);
}


/*
 * Convert a Python object to a C/C++ pointer, assuming a previous call to
 * sip_api_can_convert_to_type() has been successful.  Allow ownership to be
 * transferred and any type convertors to be disabled.
 */
void *sip_api_convert_to_type_us(PyObject *wmod, PyObject *pyObj,
        sipTypeID type_id, PyObject *transferObj, int flags, int *statep,
        void **user_statep, int *iserrp)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    return convert_to_type_us(wms, pyObj, type_id, transferObj, flags, statep,
            user_statep, iserrp);
}


/*
 * sip_api_force_convert_to_type_us() without user state support.
 */
void *sip_api_force_convert_to_type(PyObject *wmod, PyObject *pyObj,
        sipTypeID type_id, PyObject *transferObj, int flags, int *statep,
        int *iserrp)
{
    return sip_api_force_convert_to_type_us(wmod, pyObj, type_id, transferObj,
            flags, statep, NULL, iserrp);
}


/*
 * Convert a Python object to a C/C++ pointer and raise an exception if it
 * can't be done.
 */
void *sip_api_force_convert_to_type_us(PyObject *wmod, PyObject *pyObj,
        sipTypeID type_id, PyObject *transferObj, int flags, int *statep,
        void **user_statep, int *iserrp)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    return sip_force_convert_to_type_us(wms, pyObj, type_id, transferObj,
            flags, statep, user_statep, iserrp);
}


/*
 * Convert a C/C++ pointer to the object that wraps it.
 */
PyObject *sip_api_get_pyobject(PyObject *wmod, void *cppPtr,
        sipTypeID type_id)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);
    const sipTypeDef *td;
    PyTypeObject *py_type = sip_get_py_type_and_type_def(wms, type_id, &td);

    return get_pyobject(wms->sip_module_state, cppPtr, py_type, td);
}


/*
 * Report a function with invalid argument types.
 */
void sip_api_no_function(PyObject *parse_err, const char *func,
        const char *doc)
{
    sip_api_no_method(parse_err, NULL, func, doc);
}


/*
 * Report a method with invalid argument types.
 */
void sip_api_no_method(PyObject *parse_err, const char *scope,
        const char *method, const char *doc)
{
    const char *sep = ".";

    if (scope == NULL)
        scope = ++sep;

    if (parse_err == NULL)
    {
        /*
         * If we have got this far without trying a parse then there must be no
         * overloads.
         */
        PyErr_Format(PyExc_TypeError, "%s%s%s() is a private method", scope,
                sep, method);
    }
    else if (PyList_Check(parse_err))
    {
        PyObject *exc;

        /* There is an entry for each overload that was tried. */
        if (PyList_GET_SIZE(parse_err) == 1)
        {
            PyObject *detail = detail_from_failure(
                    PyList_GET_ITEM(parse_err, 0));

            if (detail != NULL)
            {
                if (doc != NULL)
                {
                    PyObject *doc_obj = signature_from_docstring(doc, 0);

                    if (doc_obj != NULL)
                    {
                        exc = PyUnicode_FromFormat("%U: %U", doc_obj, detail);
                        Py_DECREF(doc_obj);
                    }
                    else
                    {
                        exc = NULL;
                    }
                }
                else
                {
                    exc = PyUnicode_FromFormat("%s%s%s(): %U", scope, sep,
                            method, detail);
                }

                Py_DECREF(detail);
            }
            else
            {
                exc = NULL;
            }
        }
        else
        {
            static const char *summary = "arguments did not match any overloaded call:";

            Py_ssize_t i;

            if (doc != NULL)
                exc = PyUnicode_FromString(summary);
            else
                exc = PyUnicode_FromFormat("%s%s%s(): %s", scope, sep, method,
                        summary);

            for (i = 0; i < PyList_GET_SIZE(parse_err); ++i)
            {
                PyObject *failure;
                PyObject *detail = detail_from_failure(
                        PyList_GET_ITEM(parse_err, i));

                if (detail != NULL)
                {
                    if (doc != NULL)
                    {
                        PyObject *doc_obj = signature_from_docstring(doc, i);

                        if (doc_obj != NULL)
                        {
                            failure = PyUnicode_FromFormat("\n  %U: %U",
                                    doc_obj, detail);

                            Py_DECREF(doc_obj);
                        }
                        else
                        {
                            Py_XDECREF(exc);
                            exc = NULL;
                            break;
                        }
                    }
                    else
                    {
                        failure = PyUnicode_FromFormat("\n  overload %zd: %U",
                                i + 1, detail);
                    }

                    Py_DECREF(detail);

                    PyUnicode_AppendAndDel(&exc, failure);
                }
                else
                {
                    Py_XDECREF(exc);
                    exc = NULL;
                    break;
                }
            }
        }

        if (exc != NULL)
        {
            PyErr_SetObject(PyExc_TypeError, exc);
            Py_DECREF(exc);
        }
    }
    else
    {
        /*
         * None is used as a marker to say that an exception has already been
         * raised.
         */
        assert(parse_err == Py_None);
    }

    Py_XDECREF(parse_err);
}


/*
 * Parse the arguments to a C/C++ function without any side effects.
 */
int sip_api_parse_args(PyObject *wmod, PyObject **parse_err_p,
        PyObject *const *args, Py_ssize_t nr_args, const char *fmt, ...)
{
    int ok;
    va_list va;

    va_start(va, fmt);
    ok = parse_kwd_args(wmod, parse_err_p, args, nr_args, NULL, NULL, NULL,
            fmt, va);
    va_end(va);

    return ok;
}


/*
 * Parse the positional and/or keyword arguments to a C/C++ function without
 * any side effects.
 */
int sip_api_parse_kwd_args(PyObject *wmod, PyObject **parse_err_p,
        PyObject *const *args, Py_ssize_t nr_args, PyObject *kwd_names,
        const char **kwd_list, PyObject **unused, const char *fmt, ...)
{
    int ok;
    va_list va;

    if (unused != NULL)
    {
        /*
         * Initialise the return of any unused keyword arguments.  This is
         * used by any ctor overload.
         */
        *unused = NULL;
    }

    va_start(va, fmt);
    ok = parse_kwd_args(wmod, parse_err_p, args, nr_args, kwd_names, kwd_list,
            unused, fmt, va);
    va_end(va);

    /* Release any unused arguments if the parse failed. */
    if (!ok && unused != NULL)
    {
        Py_XDECREF(*unused);
    }

    return ok;
}


/*
 * Parse one or a pair of arguments to a C/C++ function without any side
 * effects.
 */
int sip_api_parse_pair(PyObject *wmod, PyObject **parse_err_p, PyObject *arg_0,
        PyObject *arg_1, const char *fmt, ...)
{
    int ok;
    va_list va;

    PyObject *args[2];
    args[0] = arg_0;
    args[1] = arg_1;

    va_start(va, fmt);
    ok = parse_kwd_args(wmod, parse_err_p, args, (arg_1 != NULL ? 2 : 1), NULL,
            NULL, NULL, fmt, va);
    va_end(va);

    return ok;
}


/*
 * Parse a result object based on a format string.
 */
int sip_api_parse_result(PyObject *wmod, sip_gilstate_t gil_state,
        sipVirtErrorHandlerFunc error_handler, sipSimpleWrapper *py_self,
        PyObject *method, PyObject *res, const char *fmt, ...)
{
    int rc;

    if (res != NULL)
    {
        sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
                wmod);
        va_list va;

        va_start(va, fmt);
        rc = parse_result(wms, method, res, deref_mixin(py_self), fmt, va);
        va_end(va);

        Py_DECREF(res);
    }
    else
    {
        rc = -1;
    }

    Py_DECREF(method);

    if (rc < 0)
        sip_api_call_error_handler(error_handler, py_self, gil_state);

    SIP_RELEASE_GIL(gil_state);

    return rc;
}


/*
 * sip_api_release_type_us() without user state support.
 */
void sip_api_release_type(PyObject *wmod, void *cpp, sipTypeID type_id,
        int state)
{
    sip_api_release_type_us(wmod, cpp, type_id, state, NULL);
}


/*
 * Release a possibly temporary C/C++ instance created by a type convertor.
 */
void sip_api_release_type_us(PyObject *wmod, void *cpp, sipTypeID type_id,
        int state, void *user_state)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    release_type_us(wms, cpp, type_id, state, user_state);
}


/*
 * Release an instance.
 */
void sip_release(void *addr, const sipTypeDef *td, int state, void *user_state)
{
    if (sipTypeIsClass(td))
    {
        sipReleaseFunc rel = ((const sipClassTypeDef *)td)->ctd_release;

        /*
         * If there is no release function then it must be a C structure and we
         * can just free it.
         */
        if (rel == NULL)
            sip_api_free(addr);
        else
            rel(addr, state);
    }
    else if (sipTypeIsMapped(td))
    {
        sipReleaseUSFunc rel = ((const sipMappedTypeDef *)td)->mtd_release;

        if (rel != NULL)
            rel(addr, state, user_state);
    }
}


/*
 * Implement the conversion of a C/C++ instance to a Python instance.
 */
PyObject *sip_convert_from_type(sipWrappedModuleState *wms, void *cpp,
        sipTypeID type_id, PyObject *transferObj)
{
    const sipTypeDef *td;
    PyTypeObject *py_type = sip_get_py_type_and_type_def(wms, type_id, &td);

    assert(sipTypeIsClass(td) || sipTypeIsMapped(td));

    /* Handle None. */
    if (cpp == NULL)
    {
        Py_INCREF(Py_None);
        return Py_None;
    }

    sipSipModuleState *sms = wms->sip_module_state;

    if ((cpp = sip_get_final_address(sms, td, cpp)) == NULL)
        return NULL;

    sipConvertFromFunc cfrom = sip_get_from_convertor(py_type, td);

    if (cfrom != NULL)
        return cfrom(cpp, transferObj);

    if (sipTypeIsMapped(td))
    {
        sip_raise_no_convert_from(td);
        return NULL;
    }

    /*
     * See if we have already wrapped it.  Invoking sub-class code can be
     * expensive so we check the cache first, even though the sub-class code
     * might perform a down-cast.
     */
    PyObject *py;

    if ((py = get_pyobject(sms, cpp, py_type, td)) == NULL && sipTypeHasSCC(td))
    {
        void *orig_cpp = cpp;
        const sipTypeDef *orig_td = td;

        /* Apply the sub-class convertor. */
        py_type = convert_subclass(sms, py_type, &td, &cpp);

        /*
         * If the sub-class convertor has done something then check the cache
         * again using the modified values.
         */
        if (cpp != orig_cpp || td != orig_td)
            py = get_pyobject(sms, cpp, py_type, td);
    }

    if (py != NULL)
        Py_INCREF(py);
    else if ((py = sip_wrap_simple_instance(sms, cpp, py_type, NULL, SIP_SHARE_MAP)) == NULL)
        return NULL;

    /* Handle any ownership transfer. */
    if (transferObj != NULL)
    {
        if (transferObj == Py_None)
            sip_transfer_back(sms, py);
        else
            sip_transfer_to(sms, py, transferObj);
    }

    return py;
}


/*
 * Convert a Python object to a C/C++ pointer.
 */
void *sip_force_convert_to_type_us(sipWrappedModuleState *wms, PyObject *pyObj,
        sipTypeID type_id, PyObject *transferObj, int flags, int *statep,
        void **user_statep, int *iserrp)
{
    /* Don't even try if there has already been an error. */
    if (*iserrp)
        return NULL;

#if 0
    /* See if the object's type can be converted. */
    if (!can_convert_to_type(wms, pyObj, type_id, flags))
    {
        const sipTypeDef *td = sip_get_type_def(wms, type_id);

        if (sipTypeIsMapped(td))
            raise_no_convert_to(pyObj, td);
        else
            PyErr_Format(PyExc_TypeError, "%s cannot be converted to %s.%s",
                    Py_TYPE(pyObj)->tp_name, sipNameOfModule(td->td_module),
                    ((const sipClassTypeDef *)td)->ctd_container.cod_name);

        if (statep != NULL)
            *statep = 0;

        *iserrp = TRUE;
        return NULL;
    }
#endif

    /* Do the conversion. */
    return convert_to_type_us(wms, pyObj, type_id, transferObj, flags, statep,
            user_statep, iserrp);
}


/*
 * Implement the return of a Python reimplementation corresponding to a C/C++
 * virtual function, if any.  If one was found then the GIL is acquired.
 */
PyObject *sip_is_py_method(sipWrappedModuleState *wms, sip_gilstate_t *gil,
        char *pymc, sipSimpleWrapper **sipSelfp, const char *cname,
        const char *mname)
{
    sipSipModuleState *sms = wms->sip_module_state;

    /*
     * This is the most common case (where there is no Python reimplementation)
     * so we take a fast shortcut without acquiring the GIL.
     */
    if (*pymc != 0)
        return NULL;

    /* We might still have C++ going after the interpreter has gone. */
    if (sms->interpreter_state == NULL)
        return NULL;

    *gil = PyGILState_Ensure();

    /* Only read this when we have the GIL. */
    sipSimpleWrapper *sipSelf = *sipSelfp;

    /*
     * It's possible that the Python object has been deleted but the underlying
     * C++ instance is still working and trying to handle virtual functions.
     * Alternatively, an instance has started handling virtual functions before
     * its ctor has returned.  In either case say there is no Python
     * reimplementation.
     */
    if (sipSelf != NULL)
        sipSelf = deref_mixin(sipSelf);

    if (sipSelf == NULL)
        goto release_gil;

    /*
     * It's possible that the object's type's tp_mro is NULL.  A possible
     * circumstance is when a type has been created dynamically and the only
     * reference to it is the single instance of the type which is in the
     * process of being garbage collected.
     */
    PyTypeObject *cls = Py_TYPE(sipSelf);
    PyObject *mro = cls->tp_mro;

    if (mro == NULL)
        goto release_gil;

    /* Get any reimplementation. */

    PyObject *mname_obj = PyUnicode_FromString(mname);
    if (mname_obj == NULL)
        goto release_gil;

    /*
     * We don't use PyObject_GetAttr() because that might find the generated
     * C function before a reimplementation defined in a mixin (ie. later in
     * the MRO).  However that means we must explicitly check that the class
     * hierarchy is fully initialised.
     */
    if (sip_container_add_lazy_attrs(wms, cls, ((sipWrapperType *)cls)->wt_td) < 0)
    {
        Py_DECREF(mname_obj);
        goto release_gil;
    }

    if (sipSelf->dict != NULL)
    {
        /* Check the instance dictionary in case it has been monkey patched. */
        PyObject *reimp = PyDict_GetItem(sipSelf->dict, mname_obj);

        if (reimp != NULL && PyCallable_Check(reimp))
        {
            Py_DECREF(mname_obj);

            Py_INCREF(reimp);
            return reimp;
        }
    }

    assert(PyTuple_Check(mro));

    PyObject *reimp = NULL;
    Py_ssize_t i;

    for (i = 0; i < PyTuple_GET_SIZE(mro); ++i)
    {
        cls = (PyTypeObject *)PyTuple_GET_ITEM(mro, i);

        PyObject *cls_dict = cls->tp_dict;
        PyObject *cls_attr;

        /*
         * Check any possible reimplementation is not the wrapped C++ method or
         * a default special method implementation.
         */
        if (cls_dict != NULL && (cls_attr = PyDict_GetItem(cls_dict, mname_obj)) != NULL && Py_TYPE(cls_attr) != sms->method_descr_type && Py_TYPE(cls_attr) != &PyWrapperDescr_Type)
        {
            reimp = cls_attr;
            break;
        }
    }

    Py_DECREF(mname_obj);

    if (reimp != NULL)
    {
        /*
         * Emulate the behaviour of a descriptor to make sure we return a bound
         * method.
         */
        if (PyMethod_Check(reimp))
        {
            /* It's already a method but make sure it is bound. */
            if (PyMethod_GET_SELF(reimp) != NULL)
                Py_INCREF(reimp);
            else
                reimp = PyMethod_New(PyMethod_GET_FUNCTION(reimp),
                        (PyObject *)sipSelf);
        }
        else if (PyFunction_Check(reimp))
        {
            reimp = PyMethod_New(reimp, (PyObject *)sipSelf);
        }
        else if (Py_TYPE(reimp)->tp_descr_get)
        {
            /* It is a descriptor, so assume it will do the right thing. */
            reimp = Py_TYPE(reimp)->tp_descr_get(reimp, (PyObject *)sipSelf,
                    (PyObject *)cls);
        }
        else
        {
            /*
             * We don't know what it is so just return and assume that an
             * appropriate exception will be raised later on.
             */
            Py_INCREF(reimp);
        }
    }
    else
    {
        /* Use the fast track in future. */
        *pymc = 1;

        if (cname != NULL)
        {
            /* Note that this will only be raised once per method. */
            PyErr_Format(PyExc_NotImplementedError,
                    "%s.%s() is abstract and must be overridden", cname,
                    mname);
            PyErr_Print();
        }

        PyGILState_Release(*gil);
    }

    return reimp;

release_gil:
    PyGILState_Release(*gil);
    return NULL;
}


/*
 * Add a parse failure to the current list of exceptions.
 */
static void add_failure(PyObject **parse_err_p, sipParseFailure *failure)
{
    sipParseFailure *failure_copy;
    PyObject *failure_obj;

    /* Create the list if necessary. */
    if (*parse_err_p == NULL && (*parse_err_p = PyList_New(0)) == NULL)
    {
        failure->reason = Raised;
        return;
    }

    /*
     * Make a copy of the failure, convert it to a Python object and add it to
     * the list.  We do it this way to make it as lightweight as possible.
     */
    if ((failure_copy = sip_api_malloc(sizeof (sipParseFailure))) == NULL)
    {
        failure->reason = Raised;
        return;
    }

    *failure_copy = *failure;

    if ((failure_obj = PyCapsule_New(failure_copy, NULL, failure_dtor)) == NULL)
    {
        sip_api_free(failure_copy);
        failure->reason = Raised;
        return;
    }

    /* Ownership of any detail object is now with the wrapped failure. */
    failure->detail_obj = NULL;

    if (PyList_Append(*parse_err_p, failure_obj) < 0)
    {
        Py_DECREF(failure_obj);
        failure->reason = Raised;
        return;
    }

    Py_DECREF(failure_obj);
}


/*
 * Return a string as a Python object that describes an argument with an
 * unexpected type.
 */
static PyObject *bad_type_str(int arg_nr, PyObject *arg)
{
    return PyUnicode_FromFormat("argument %d has unexpected type '%s'", arg_nr,
            Py_TYPE(arg)->tp_name);
}


/*
 * Get the values off the stack and put them into an object.
 */
static PyObject *build_object(sipWrappedModuleState *wms, PyObject *obj,
        const char *fmt, va_list va)
{
    /*
     * The format string has already been checked that it is properly formed if
     * it is enclosed in parenthesis.
     */
    char term_ch;

    if (*fmt == '(')
    {
        term_ch = ')';
        ++fmt;
    }
    else
        term_ch = '\0';

    char ch;
    int i = 0;

    while ((ch = *fmt++) != term_ch)
    {
        PyObject *el;

        switch (ch)
        {
        case 'g':
            {
                char *s = va_arg(va, char *);
                Py_ssize_t l = va_arg(va, Py_ssize_t);

                if (s != NULL)
                {
                    el = PyBytes_FromStringAndSize(s, l);
                }
                else
                {
                    Py_INCREF(Py_None);
                    el = Py_None;
                }
            }

            break;

        case 'G':
            {
                wchar_t *s = va_arg(va, wchar_t *);
                Py_ssize_t l = va_arg(va, Py_ssize_t);

                if (s != NULL)
                    el = PyUnicode_FromWideChar(s, l);
                else
                {
                    Py_INCREF(Py_None);
                    el = Py_None;
                }
            }

            break;

        case 'b':
            el = PyBool_FromLong(va_arg(va, int));
            break;

        case 'c':
            {
                char c = va_arg(va, int);

                el = PyBytes_FromStringAndSize(&c, 1);
            }

            break;

        case 'a':
            {
                char c = va_arg(va, int);

                el = PyUnicode_FromStringAndSize(&c, 1);
            }

            break;

        case 'w':
            {
                wchar_t c = va_arg(va, int);

                el = PyUnicode_FromWideChar(&c, 1);
            }

            break;

        case 'F':
            {
                int ev = va_arg(va, int);
                sipTypeID type_id = va_arg(va, sipTypeID);

                el = sip_enum_convert_from_enum(wms, ev, type_id);
            }

            break;

        case 'd':
        case 'f':
            el = PyFloat_FromDouble(va_arg(va, double));
            break;

        case 'e':
        case 'h':
        case 'i':
        case 'L':
            el = PyLong_FromLong(va_arg(va, int));
            break;

        case 'l':
            el = PyLong_FromLong(va_arg(va, long));
            break;

        case 'm':
            el = PyLong_FromUnsignedLong(va_arg(va, unsigned long));
            break;

        case 'n':
            el = PyLong_FromLongLong(va_arg(va, long long));
            break;

        case 'o':
            el = PyLong_FromUnsignedLongLong(va_arg(va, unsigned long long));
            break;

        case 's':
            {
                char *s = va_arg(va, char *);

                if (s != NULL)
                {
                    el = PyBytes_FromString(s);
                }
                else
                {
                    Py_INCREF(Py_None);
                    el = Py_None;
                }
            }

            break;

        case 'A':
            {
                char *s = va_arg(va, char *);

                if (s != NULL)
                {
                    el = PyUnicode_FromString(s);
                }
                else
                {
                    Py_INCREF(Py_None);
                    el = Py_None;
                }
            }

            break;

        case 'x':
            {
                wchar_t *s = va_arg(va, wchar_t *);

                if (s != NULL)
                    el = PyUnicode_FromWideChar(s, (Py_ssize_t)wcslen(s));
                else
                {
                    Py_INCREF(Py_None);
                    el = Py_None;
                }
            }

            break;

        case 't':
        case 'u':
        case 'M':
            el = PyLong_FromUnsignedLong(va_arg(va, unsigned));
            break;

        case '=':
            el = PyLong_FromSize_t(va_arg(va, size_t));
            break;

        case 'N':
            {
                void *p = va_arg(va, void *);
                sipTypeID type_id = va_arg(va, sipTypeID);
                PyObject *xfer = va_arg(va, PyObject *);

                el = convert_from_new_type(wms, p, type_id, xfer);
            }

            break;

        case 'D':
            {
                void *p = va_arg(va, void *);
                const sipTypeID type_id = va_arg(va, sipTypeID);
                PyObject *xfer = va_arg(va, PyObject *);

                el = sip_convert_from_type(wms, p, type_id, xfer);
            }

            break;

        case 'r':
            {
                void *p = va_arg(va, void *);
                Py_ssize_t l = va_arg(va, Py_ssize_t);
                sipTypeID type_id = va_arg(va, sipTypeID);

                el = convert_to_sequence(wms, p, l, type_id);
            }

            break;

        case 'R':
            el = va_arg(va, PyObject *);
            break;

        case 'S':
            el = va_arg(va, PyObject *);
            Py_INCREF(el);
            break;

        case 'V':
            el = sip_convert_from_void_ptr(wms->sip_module_state,
                    va_arg(va, void *));
            break;

        case 'z':
            {
                const char *name = va_arg(va, const char *);
                void *p = va_arg(va, void *);

                if (p == NULL)
                {
                    el = Py_None;
                    Py_INCREF(el);
                }
                else
                {
                    el = PyCapsule_New(p, name, NULL);
                }
            }

            break;

        default:
            PyErr_Format(PyExc_SystemError,
                    "build_object(): invalid format character '%c'", ch);
            el = NULL;
        }

        if (el == NULL)
        {
            Py_XDECREF(obj);
            return NULL;
        }

        if (obj == NULL)
            return el;

        PyTuple_SET_ITEM(obj, i, el);
        i++;
    }

    return obj;
}


/*
 * Call a method and return the result.
 */
static PyObject *call_method(sipWrappedModuleState *wms, PyObject *method,
        const char *fmt, va_list va)
{
    PyObject *args, *res;

    if ((args = PyTuple_New(strlen(fmt))) == NULL)
        return NULL;

    if (build_object(wms, args, fmt, va) != NULL)
        res = PyObject_CallObject(method, args);
    else
        res = NULL;

    Py_DECREF(args);

    return res;
}


/*
 * See if a Python object is a sequence of a particular type.
 */
static int can_convert_from_sequence(sipWrappedModuleState *wms, PyObject *seq,
        sipTypeID type_id)
{
    Py_ssize_t i, size = PySequence_Size(seq);

    if (size < 0)
        return FALSE;

    /*
     * Check the type of each element.  Note that this is inconsistent with how
     * similiar situations are handled elsewhere.  We should instead just check
     * we have an iterator and assume (until the second pass) that the type is
     * correct.
     */
    // TODO So fix it.
    for (i = 0; i < size; ++i)
    {
        int ok;
        PyObject *val_obj;

        if ((val_obj = PySequence_GetItem(seq, i)) == NULL)
            return FALSE;

        ok = can_convert_to_type(wms, val_obj, type_id,
                SIP_NO_CONVERTORS|SIP_NOT_NONE);

        Py_DECREF(val_obj);

        if (!ok)
            return FALSE;
    }

    return TRUE;
}


/*
 * Implement the check to see if a Python object can be converted to a type.
 */
static int can_convert_to_type(sipWrappedModuleState *wms, PyObject *pyObj,
        sipTypeID type_id, int flags)
{
    const sipTypeDef *td;
    PyTypeObject *py_type = sip_get_py_type_and_type_def(wms, type_id, &td);

    assert(td == NULL || sipTypeIsClass(td) || sipTypeIsMapped(td));

    int ok;

    if (td == NULL)
    {
        /*
         * The type must be /External/ and the module that contains the
         * implementation hasn't been imported.
         */
        ok = FALSE;
    }
    else if (pyObj == Py_None)
    {
        /* If the type explicitly handles None then ignore the flags. */
        if (sipTypeAllowNone(td))
            ok = TRUE;
        else
            ok = ((flags & SIP_NOT_NONE) == 0);
    }
    else
    {
        sipConvertToFunc cto;

        if (sipTypeIsClass(td))
        {
            cto = ((const sipClassTypeDef *)td)->ctd_cto;

            if (cto == NULL || (flags & SIP_NO_CONVERTORS) != 0)
                ok = PyObject_TypeCheck(pyObj, py_type);
            else
                ok = cto(pyObj, NULL, NULL, NULL, NULL);
        }
        else
        {
            if ((cto = ((const sipMappedTypeDef *)td)->mtd_cto) != NULL)
                ok = cto(pyObj, NULL, NULL, NULL, NULL);
            else
                ok = FALSE;
        }
    }

    return ok;
}


/*
 * Check if an object is of the right type to convert to an encoded string.
 */
static int check_encoded_string(PyObject *obj)
{
    Py_buffer view;

    if (obj == Py_None)
        return 0;

    if (PyUnicode_Check(obj))
        return 0;

    if (PyBytes_Check(obj))
        return 0;

    if (PyObject_GetBuffer(obj, &view, PyBUF_SIMPLE) < 0)
    {
        PyErr_Clear();
    }
    else
    {
        PyBuffer_Release(&view);
        return 0;
    }

    return -1;
}


/*
 * Implement the conversion of a new C/C++ instance to a Python instance.
 */
static PyObject *convert_from_new_type(sipWrappedModuleState *wms, void *cpp,
        sipTypeID type_id, PyObject *transferObj)
{
    /* Handle None. */
    if (cpp == NULL)
    {
        Py_INCREF(Py_None);
        return Py_None;
    }

    const sipTypeDef *td;
    PyTypeObject *py_type = sip_get_py_type_and_type_def(wms, type_id, &td);
    sipSipModuleState *sms = wms->sip_module_state;

    if ((cpp = sip_get_final_address(sms, td, cpp)) == NULL)
        return NULL;

    sipConvertFromFunc cfrom = sip_get_from_convertor(py_type, td);

    if (cfrom != NULL)
    {
        PyObject *res = cfrom(cpp, transferObj);

        if (res != NULL)
        {
            /*
             * We no longer need the C/C++ instance so we release it (unless
             * its ownership is transferred).  This means this call is
             * semantically equivalent to the case where we are wrapping a
             * class.
             */
            if (transferObj == NULL || transferObj == Py_None)
                sip_release(cpp, td, 0, NULL);
        }

        return res;
    }

    if (sipTypeIsMapped(td))
    {
        sip_raise_no_convert_from(td);
        return NULL;
    }

    /* Apply any sub-class convertor. */
    if (sipTypeHasSCC(td))
        py_type = convert_subclass(sms, py_type, &td, &cpp);

    /* Handle any ownership transfer. */
    sipWrapper *owner;

    if (transferObj == NULL || transferObj == Py_None)
        owner = NULL;
    else
        owner = (sipWrapper *)transferObj;

    return sip_wrap_simple_instance(sms, cpp, py_type, owner,
            (owner == NULL ? SIP_PY_OWNED : 0));
}


/*
 * Convert a Python sequence to an array that has already "passed"
 * can_convert_from_sequence().  Return TRUE if the conversion was successful.
 */
static int convert_from_sequence(sipWrappedModuleState *wms, PyObject *seq,
        sipTypeID type_id, void **array, Py_ssize_t *nr_elem)
{
    const sipTypeDef *td = sip_get_type_def(wms, type_id);

    sipArrayFunc array_helper;
    sipAssignFunc assign_helper;

    /* Get the type's helpers. */
    if (sipTypeIsMapped(td))
    {
        array_helper = ((const sipMappedTypeDef *)td)->mtd_array;
        assign_helper = ((const sipMappedTypeDef *)td)->mtd_assign;
    }
    else
    {
        array_helper = ((const sipClassTypeDef *)td)->ctd_array;
        assign_helper = ((const sipClassTypeDef *)td)->ctd_assign;
    }

    assert(array_helper != NULL);
    assert(assign_helper != NULL);

    /*
     * Create the memory for the array of values.  Note that this will leak if
     * there is an error.
     */
    int iserr = 0;
    Py_ssize_t size = PySequence_Size(seq);
    void *array_mem = array_helper(size);
    Py_ssize_t i;

    for (i = 0; i < size; ++i)
    {
        PyObject *val_obj;
        void *val;

        if ((val_obj = PySequence_GetItem(seq, i)) == NULL)
            return FALSE;

        val = convert_to_type_us(wms, val_obj, type_id, NULL,
                SIP_NO_CONVERTORS|SIP_NOT_NONE, NULL, NULL, &iserr);

        Py_DECREF(val_obj);

        if (iserr)
            return FALSE;

        assign_helper(array_mem, i, val);
    }

    *array = array_mem;
    *nr_elem = size;

    return TRUE;
}


/*
 * Call any sub-class convertors for a type returning an updated Python type
 * object and type definition corresponding to the sub-type, and possibly
 * modifying the C++ address (in the case of multiple inheritence).
 */
static PyTypeObject *convert_subclass(sipSipModuleState *sms,
        PyTypeObject *py_type, const sipTypeDef **td_p, void **cppPtr_p)
{
    /* Handle the trivial case. */
    if (*cppPtr_p == NULL)
        return NULL;

    /* Try the conversions until told to stop. */
    while (convert_subclass_pass(sms, &py_type, td_p, cppPtr_p))
        ;

    return py_type;
}


/*
 * Do a single pass through the available convertors.
 */
static int convert_subclass_pass(sipSipModuleState *sms,
        PyTypeObject **py_type_p, const sipTypeDef **td_p, void **cppPtr_p)
{
    PyTypeObject *py_type = *py_type_p;

    /*
     * Note that this code depends on the fact that a module appears in the
     * list of modules before any module it imports, ie. sub-class convertors
     * will be invoked for more specific types first.
     */
    Py_ssize_t i;

    for (i = 0; i < PyList_GET_SIZE(sms->module_list); i++)
    {
        PyObject *mod = PyList_GET_ITEM(sms->module_list, i);
        sipWrappedModuleState *ms = (sipWrappedModuleState *)PyModule_GetState(
                mod);
        const sipSubClassConvertorDef *scc = ms->wrapped_module_def->convertors;

        if (scc == NULL)
            continue;

        while (scc->scc_convertor != NULL)
        {
            const sipTypeDef *base_td;
            PyTypeObject *base_type = sip_get_py_type_and_type_def(ms,
                    scc->scc_base, &base_td);

            /*
             * The base type is the "root" class that may have a number of
             * convertors each handling a "branch" of the derived tree of
             * classes.  The "root" normally implements the base function that
             * provides the RTTI used by the convertors and is re-implemented
             * by derived classes.  We therefore see if the target type is a
             * sub-class of the root, ie. see if the convertor might be able to
             * convert the target type to something more specific.
             */
            if (PyType_IsSubtype(py_type, base_type))
            {
                void *ptr = sip_cast_cpp_ptr(*cppPtr_p, py_type, base_td);
                PyObject *wmod;
                sipTypeID sub_id;

                if ((wmod = (*scc->scc_convertor)(&ptr, &sub_id)) != NULL)
                {
                    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
                        wmod);
                    const sipTypeDef *sub_td;
                    PyTypeObject *sub_type = sip_get_py_type_and_type_def(wms,
                            sub_id, &sub_td);

                    /*
                     * We are only interested in types that are not
                     * super-classes of the target.  This happens either
                     * because it is in an earlier convertor than the one that
                     * handles the type or it is in a later convertor that
                     * handles a different branch of the hierarchy.  Either
                     * way, the ordering of the modules ensures that there will
                     * be no more than one and that it will be the right one.
                     */
                    if (!PyType_IsSubtype(py_type, sub_type))
                    {
                        *py_type_p = sub_type;
                        *td_p = sub_td;
                        *cppPtr_p = ptr;

                        /*
                         * Finally we allow the convertor to return a type that
                         * is apparently unrelated to the current convertor.
                         * This causes the whole process to be restarted with
                         * the new values.  The use case is PyQt's QLayoutItem.
                         */
                        return !PyType_IsSubtype(sub_type, base_type);
                    }
                }
            }

            scc++;
        }
    }

    /*
     * We haven't found the exact type, so return the most specific type that
     * it must be.  This can happen legitimately if the wrapped library is
     * returning an internal class that is down-cast to a more generic class.
     * Also we want this function to be safe when a class doesn't have any
     * convertors.
     */
    return FALSE;
}


/*
 * Convert an array of a type to a Python sequence.
 */
static PyObject *convert_to_sequence(sipWrappedModuleState *wms, void *array,
        Py_ssize_t nr_elem, sipTypeID type_id)
{
    const sipTypeDef *td = sip_get_type_def(wms, type_id);

    sipCopyFunc copy_helper;

    /* Get the type's copy helper. */
    if (sipTypeIsMapped(td))
        copy_helper = ((const sipMappedTypeDef *)td)->mtd_copy;
    else
        copy_helper = ((const sipClassTypeDef *)td)->ctd_copy;

    assert(copy_helper != NULL);

    PyObject *seq = PyTuple_New(nr_elem);
    if (seq == NULL)
        return NULL;

    Py_ssize_t i;

    for (i = 0; i < nr_elem; ++i)
    {
        void *el = copy_helper(array, i);
        PyObject *el_obj = convert_from_new_type(wms, el, type_id, NULL);

        if (el_obj == NULL)
        {
            sip_release(el, td, 0, NULL);
            Py_DECREF(seq);
        }

        PyTuple_SET_ITEM(seq, i, el_obj);
    }

    return seq;
}


/*
 * Convert a Python object to a C/C++ pointer.
 */
static void *convert_to_type_us(sipWrappedModuleState *wms, PyObject *pyObj,
        sipTypeID type_id, PyObject *transferObj, int flags, int *statep,
        void **user_statep, int *iserrp)
{
    const sipTypeDef *td = sip_get_type_def(wms, type_id);

    assert(sipTypeIsClass(td) || sipTypeIsMapped(td));

    void *cpp = NULL;
    int state = 0;

    /* Don't convert if there has already been an error. */
    if (!*iserrp)
    {
        /* Do the conversion. */
        if (pyObj == Py_None && !sipTypeAllowNone(td))
        {
            cpp = NULL;
        }
        else
        {
            sipConvertToFunc cto;

            if (sipTypeIsClass(td))
            {
                cto = ((const sipClassTypeDef *)td)->ctd_cto;

                if (cto == NULL || (flags & SIP_NO_CONVERTORS) != 0)
                {
                    if ((cpp = sip_get_cpp_ptr(wms, (sipSimpleWrapper *)pyObj, type_id)) == NULL)
                    {
                        *iserrp = TRUE;
                    }
                    else if (transferObj != NULL)
                    {
                        if (transferObj == Py_None)
                            sip_transfer_back(wms->sip_module_state, pyObj);
                        else
                            sip_transfer_to(wms->sip_module_state, pyObj,
                                    transferObj);
                    }
                }
                else if (user_state_is_valid(td, user_statep))
                {
                    state = cto(pyObj, &cpp, iserrp, transferObj, user_statep);
                }
            }
            else if ((cto = ((const sipMappedTypeDef *)td)->mtd_cto) != NULL)
            {
                if (user_state_is_valid(td, user_statep))
                    state = cto(pyObj, &cpp, iserrp, transferObj, user_statep);
            }
            else
            {
                    raise_no_convert_to(pyObj, td);
            }
        }
    }

    if (statep != NULL)
        *statep = state;

    return cpp;
}


/*
 * Return the main instance for an object if it is a mixin.
 */
static sipSimpleWrapper *deref_mixin(sipSimpleWrapper *w)
{
    return w->mixin_main != NULL ? (sipSimpleWrapper *)w->mixin_main : w;
}


/*
 * Return a string/unicode object that describes the given failure.
 */
static PyObject *detail_from_failure(PyObject *failure_obj)
{
    sipParseFailure *failure;
    PyObject *detail;

    failure = (sipParseFailure *)PyCapsule_GetPointer(failure_obj, NULL);

    switch (failure->reason)
    {
    case Unbound:
        detail = PyUnicode_FromFormat(
                "first argument of unbound method must have type '%s'",
                failure->detail_str);
        break;

    case TooFew:
        detail = PyUnicode_FromString("not enough arguments");
        break;

    case TooMany:
        detail = PyUnicode_FromString("too many arguments");
        break;

    case KeywordNotString:
        detail = PyUnicode_FromFormat(
                "%S keyword argument name is not a string",
                failure->detail_obj);
        break;

    case UnknownKeyword:
        detail = PyUnicode_FromFormat("'%U' is not a valid keyword argument",
                failure->detail_obj);
        break;

    case Duplicate:
        detail = PyUnicode_FromFormat(
                "'%U' has already been given as a positional argument",
                failure->detail_obj);
        break;

    case WrongType:
        if (failure->arg_nr >= 0)
            detail = bad_type_str(failure->arg_nr, failure->detail_obj);
        else
            detail = PyUnicode_FromFormat(
                    "argument '%s' has unexpected type '%s'",
                    failure->arg_name, Py_TYPE(failure->detail_obj)->tp_name);

        break;

    case Exception:
        detail = failure->detail_obj;

        if (detail)
        {
            Py_INCREF(detail);
            break;
        }

        /* Drop through. */

    default:
        detail = PyUnicode_FromString("unknown reason");
    }

    return detail;
}


/*
 * The dtor for parse failure wrapped in a Python object.
 */
static void failure_dtor(PyObject *capsule)
{
    sipParseFailure *failure = (sipParseFailure *)PyCapsule_GetPointer(capsule, NULL);

    Py_XDECREF(failure->detail_obj);

    sip_api_free(failure);
}


/*
 * Return the value of a keyword argument.
 */
static PyObject *get_kwd_arg(PyObject *const *args, Py_ssize_t nr_args,
        PyObject *kwd_names, Py_ssize_t nr_kwd_names, const char *name)
{
    Py_ssize_t i;

    for (i = 0; i < nr_kwd_names; i++)
    {
        PyObject *kwd_name = PyTuple_GET_ITEM(kwd_names, i);

        if (PyUnicode_CompareWithASCIIString(kwd_name, name) == 0)
            return args[nr_args + i];
    }

    return NULL;
}


/*
 * Implement the conversion of a C/C++ pointer to the object that wraps it.
 */
static PyObject *get_pyobject(sipSipModuleState *sms, void *cppPtr,
        PyTypeObject *py_type, const sipTypeDef *td)
{
    return (PyObject *)sip_om_find_object(&sms->object_map, cppPtr, py_type,
            td);
}


/*
 * Get "self" from the argument array for a method called as
 * Class.Method(self, ...) rather than self.Method(...).
 */
static int get_self_from_args(PyTypeObject *py_type, PyObject *const *args,
        Py_ssize_t nr_args, Py_ssize_t arg_nr, PyObject **self_p)
{
    if (arg_nr >= nr_args)
        return FALSE;

    PyObject *self = args[arg_nr];

    if (!PyObject_TypeCheck(self, py_type))
        return FALSE;

    *self_p = self;

    return TRUE;
}


/*
 * Called after a failed conversion of an integer.
 */
static void handle_failed_int_conversion(sipParseFailure *pf, PyObject *arg)
{
    PyObject *xtype, *xvalue, *xtb;

    assert(pf->reason == Ok || pf->reason == Overflow);

    PyErr_Fetch(&xtype, &xvalue, &xtb);

    if (PyErr_GivenExceptionMatches(xtype, PyExc_OverflowError) && xvalue != NULL)
    {
        /* Remove any previous overflow exception. */
        Py_XDECREF(pf->detail_obj);

        pf->reason = Overflow;
        pf->overflow_arg_nr = pf->arg_nr;
        pf->overflow_arg_name = pf->arg_name;
        pf->detail_obj = xvalue;
        Py_INCREF(xvalue);
    }
    else
    {
        handle_failed_type_conversion(pf, arg);
    }

    PyErr_Restore(xtype, xvalue, xtb);
}


/*
 * Called after a failed conversion of a type.
 */
static void handle_failed_type_conversion(sipParseFailure *pf, PyObject *arg)
{
    pf->reason = WrongType;
    pf->detail_obj = arg;
    Py_INCREF(arg);
}


/*
 * Parse the arguments to a C/C++ function without any side effects.
 */
static int parse_kwd_args(PyObject *wmod, PyObject **parse_err_p,
        PyObject *const *args, Py_ssize_t nr_args, PyObject *kwd_names,
        const char **kwd_list, PyObject **unused, const char *fmt,
        va_list va_orig)
{
    /* Previous second pass errors stop subsequent parses. */
    if (*parse_err_p != NULL && !PyList_Check(*parse_err_p))
        return FALSE;

    /* Get the number of keyword names given. */
    Py_ssize_t nr_kwd_names;

    if (kwd_names != NULL)
    {
        assert(PyTuple_Check(kwd_names));
        nr_kwd_names = PyTuple_GET_SIZE(kwd_names);
    }
    else
    {
        nr_kwd_names = 0;
    }

    /*
     * The first pass checks all the types and does conversions that are cheap
     * and have no side effects.
     */
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);
    int ok, self_in_args;
    PyObject *self;
    va_list va;

    va_copy(va, va_orig);
    ok = parse_pass_1(wms, parse_err_p, &self, &self_in_args, args, nr_args,
            kwd_names, nr_kwd_names, kwd_list, unused, fmt, va);
    va_end(va);

    if (ok)
    {
        /*
         * The second pass does any remaining conversions now that we know we
         * have the right signature.
         */
        va_copy(va, va_orig);
        ok = parse_pass_2(wms, self, self_in_args, args, nr_args, kwd_names,
                nr_kwd_names, kwd_list, fmt, va);
        va_end(va);

        /* Remove any previous failed parses. */
        Py_XDECREF(*parse_err_p);

        if (ok)
        {
            *parse_err_p = NULL;
        }
        else
        {
            /* Indicate that an exception has been raised. */
            *parse_err_p = Py_None;
            Py_INCREF(Py_None);
        }
    }

    return ok;
}


/*
 * First pass of the argument parse, converting those that can be done so
 * without any side effects.  Return TRUE if the arguments matched.
 */
static int parse_pass_1(sipWrappedModuleState *wms, PyObject **parse_err_p,
        PyObject **self_p, int *self_in_args_p, PyObject *const *args,
        Py_ssize_t nr_args, PyObject *kwd_names, Py_ssize_t nr_kwd_names,
        const char **kwd_list, PyObject **unused, const char *fmt, va_list va)
{
    sipSipModuleState *sms = wms->sip_module_state;
    int compulsory = TRUE;
    Py_ssize_t arg_nr = 0;
    Py_ssize_t nr_kwd_names_used = 0;
    sipParseFailure failure = {
        .reason = Ok,
        .detail_obj = NULL,
    };

    /*
     * Handle those format characters that deal with the "self" argument.  They
     * will always be the first one.
     */
    *self_p = NULL;
    *self_in_args_p = FALSE;

    switch (*fmt++)
    {
    case '#':
            /* A ctor has an argument with the /Transfer/ annotation. */
            *self_p = va_arg(va, PyObject *);
            break;

    case 'B':
    case 'p':
        {
            PyObject *self = *va_arg(va, PyObject **);
            sipTypeID type_id = va_arg(va, sipTypeID);
            va_arg(va, void **);

            PyTypeObject *py_type = sip_get_py_type(wms, type_id);

            if (self != NULL && PyObject_TypeCheck(self, sms->simple_wrapper_type))
            {
                /* The call was self.method(...). */
                *self_p = self;
            }
            else if (get_self_from_args(py_type, args, nr_args, arg_nr, self_p))
            {
                /* The call was cls.method(self, ...). */
                *self_in_args_p = TRUE;
                ++arg_nr;
            }
            else
            {
                failure.reason = Unbound;
                failure.detail_str = py_type->tp_name;
            }

            break;
        }

    case 'C':
        {
            PyObject *self;

            self = *va_arg(va, PyObject **);

            /*
             * If the call was self.method(...) rather than cls.method(...)
             * then get cls from self.
             */
            if (PyObject_TypeCheck(self, sms->wrapper_type))
                self = (PyObject *)Py_TYPE(self);

            *self_p = self;

            break;
        }

    default:
        --fmt;
    }

    /*
     * Now handle the remaining arguments.  We continue to parse if we get an
     * overflow because that is, strictly speaking, a second pass error.
     */
    while (failure.reason == Ok || failure.reason == Overflow)
    {
        char ch;

        // TODO This shouldn't be necessary if all conversions don't make an
        // assumption about the current error state.
        PyErr_Clear();

        /* See if the following arguments are optional. */
        if ((ch = *fmt++) == '|')
        {
            compulsory = FALSE;
            ch = *fmt++;
        }

        /* See if we don't expect anything else. */

        if (ch == '\0')
        {
            if (arg_nr < nr_args)
            {
                /* There are still positional arguments. */
                failure.reason = TooMany;
            }
            else if (nr_kwd_names_used != nr_kwd_names)
            {
                /*
                 * Take a shortcut if no keyword arguments were used and we are
                 * interested in them.
                 */
                if (nr_kwd_names_used == 0 && unused != NULL)
                {
                    // TODO unused now has a different type.  Do we create a
                    // dict to return the names and values? (Would also
                    // maintain compatibility if convenient.)
                    Py_INCREF(kwd_names);
                    *unused = kwd_names;
                }
                else
                {
                    /*
                     * Go through the keyword arguments to find any that were
                     * duplicates of positional arguments.  For the remaining
                     * ones remember the unused ones if we are interested.
                     */
                    PyObject *unused_dict = NULL;
                    Py_ssize_t pos;

                    for (pos = 0; pos < nr_kwd_names; pos++)
                    {
                        PyObject *kwd_name = PyTuple_GET_ITEM(kwd_names, pos);
                        PyObject *kwd_value = args[nr_args + pos];

                        if (!PyUnicode_Check(kwd_name))
                        {
                            failure.reason = KeywordNotString;
                            failure.detail_obj = kwd_name;
                            Py_INCREF(kwd_name);
                            break;
                        }

                        Py_ssize_t a;

                        if (kwd_list != NULL)
                        {
                            /* Get the argument's index if it is one. */
                            for (a = 0; a < nr_args; ++a)
                            {
                                const char *name = kwd_list[a];

                                if (name == NULL)
                                    continue;

                                if (PyUnicode_CompareWithASCIIString(kwd_name, name) == 0)
                                    break;
                            }
                        }
                        else
                        {
                            a = nr_args;
                        }

                        if (a == nr_args)
                        {
                            /*
                             * The name doesn't correspond to a keyword
                             * argument.
                             */
                            if (unused == NULL)
                            {
                                /*
                                 * It may correspond to a keyword argument of a
                                 * different overload.
                                 */
                                failure.reason = UnknownKeyword;
                                failure.detail_obj = kwd_name;
                                Py_INCREF(kwd_name);

                                break;
                            }

                            /*
                             * Add it to the dictionary of unused arguments
                             * creating it if necessary.  Note that if the
                             * unused arguments are actually used by a later
                             * overload then the parse will incorrectly
                             * succeed.  This should be picked up (perhaps with
                             * a misleading exception) so long as the code that
                             * handles the unused arguments checks that it can
                             * handle them all.
                             */
                            if (unused_dict == NULL && (*unused = unused_dict = PyDict_New()) == NULL)
                            {
                                failure.reason = Raised;
                                break;
                            }

                            if (PyDict_SetItem(unused_dict, kwd_name, kwd_value) < 0)
                            {
                                failure.reason = Raised;
                                break;
                            }
                        }
                        else if (a < nr_args - *self_in_args_p)
                        {
                            /*
                             * The argument has been given positionally and as
                             * a keyword.
                             */
                            failure.reason = Duplicate;
                            failure.detail_obj = kwd_name;
                            Py_INCREF(kwd_name);
                            break;
                        }
                    }
                }
            }

            break;
        }

        /* Get the next argument. */
        PyObject *arg = NULL;
        failure.arg_nr = -1;
        failure.arg_name = NULL;

        if (arg_nr < nr_args)
        {
            /* It's a positional argument. */
            arg = args[arg_nr];
            failure.arg_nr = arg_nr + 1;
        }
        else if (nr_kwd_names != 0 && kwd_list != NULL)
        {
            // TODO Review this to eliminate the NULLs from kwd_list.  We don't
            // know initially how many positional arguments are required
            // (because that requires decoding the format string) but we will
            // know by now (the 'compulsory' flag).

            const char *name = kwd_list[arg_nr - *self_in_args_p];

            if (name != NULL)
            {
                arg = get_kwd_arg(args, nr_args, kwd_names, nr_kwd_names,
                        name);

                if (arg != NULL)
                    ++nr_kwd_names_used;

                failure.arg_name = name;
            }
        }

        ++arg_nr;

        if (arg == NULL && compulsory)
        {
            if (ch == 'W')
            {
                /*
                 * A variable number of arguments was allowed but none were
                 * given.
                 */
                break;
            }

            /* An argument was required. */
            failure.reason = TooFew;

            /*
             * Check if there were any unused keyword arguments so that we give
             * a (possibly) more accurate diagnostic in the case that a keyword
             * argument has been mis-spelled.
             */
            if (unused == NULL && kwd_names != NULL && nr_kwd_names_used != nr_kwd_names)
            {
#if 0
                // TODO
                PyObject *key, *value;
                Py_ssize_t pos = 0;

                while (PyDict_Next(kwd_args, &pos, &key, &value))
                {
                    int a;

                    if (!PyUnicode_Check(key))
                    {
                        failure.reason = KeywordNotString;
                        failure.detail_obj = key;
                        Py_INCREF(key);
                        break;
                    }

                    if (kwd_list != NULL)
                    {
                        /* Get the argument's index if it is one. */
                        for (a = 0; a < nr_args; ++a)
                        {
                            const char *name = kwd_list[a];

                            if (name == NULL)
                                continue;

                            if (PyUnicode_CompareWithASCIIString(key, name) == 0)
                                break;
                        }
                    }
                    else
                    {
                        a = nr_args;
                    }

                    if (a == nr_args)
                    {
                        failure.reason = UnknownKeyword;
                        failure.detail_obj = key;
                        Py_INCREF(key);

                        break;
                    }
                }
#endif
            }

            break;
        }

        /*
         * Handle the format character even if we don't have an argument so
         * that we skip the right number of arguments.
         */
        switch (ch)
        {
        case 'W':
            /* Ellipsis. */
            break;

        case '@':
            {
                /* Implement /GetWrapper/. */

                PyObject **p = va_arg(va, PyObject **);

                if (arg != NULL)
                    *p = arg;

                /* Process the same argument next time round. */
                --arg_nr;

                break;
            }

        case 's':
            {
                /* String from a Python bytes or None. */

                const char **p = va_arg(va, const char **);

                if (arg != NULL)
                {
                    const char *cp = sip_api_bytes_as_string(arg);

                    if (PyErr_Occurred())
                        handle_failed_type_conversion(&failure, arg);
                    else
                        *p = cp;
                }

                break;
            }

        case 'A':
            {
                /* String from a Python string or None. */

                va_arg(va, PyObject **);
                va_arg(va, const char **);
                fmt++;

                if (arg != NULL && check_encoded_string(arg) < 0)
                    handle_failed_type_conversion(&failure, arg);

                break;
            }

        case 'a':
            {
                /* Character from a Python string. */

                va_arg(va, char *);
                fmt++;

                if (arg != NULL && check_encoded_string(arg) < 0)
                    handle_failed_type_conversion(&failure, arg);

                break;
            }

        case 'x':
            {
                /* Wide string or None. */

                PyObject **keep_p = va_arg(va, PyObject **);
                wchar_t **p = va_arg(va, wchar_t **);

                if (arg != NULL)
                {
                    PyObject *keep = arg;

                    wchar_t *wcp = sip_api_string_as_wstring(&keep);

                    if (PyErr_Occurred())
                    {
                        handle_failed_type_conversion(&failure, arg);
                    }
                    else
                    {
                        *keep_p = keep;
                        *p = wcp;
                    }
                }
                break;
            }

        case 'r':
            {
                /*
                 * Sequence of mapped type instances.  For ABI v13.3 and
                 * earlier this is also used for class instances.
                 */

                sipTypeID type_id = va_arg(va, sipTypeID);
                va_arg(va, void **);
                va_arg(va, Py_ssize_t *);

                if (arg != NULL && !can_convert_from_sequence(wms, arg, type_id))
                    handle_failed_type_conversion(&failure, arg);

                break;
            }

        case '>':
            {
                /*
                 * Sequence or sip.array of class instances.  This is only used
                 * by ABI v13.4 and later.
                 */

                sipTypeID type_id = va_arg(va, sipTypeID);
                va_arg(va, void **);
                va_arg(va, Py_ssize_t *);
                va_arg(va, int *);

                if (arg != NULL && !sip_array_can_convert(wms, arg, type_id) && !can_convert_from_sequence(wms, arg, type_id))
                    handle_failed_type_conversion(&failure, arg);

                break;
            }

        case 'J':
            {
                /* Class or mapped type instance. */

                sipTypeID type_id = va_arg(va, sipTypeID);
                va_arg(va, void **);

                char sub_fmt = *fmt++;
                int flags = sub_fmt - '0';
                int iflgs = 0;

                if (flags & FMT_AP_DEREF)
                    iflgs |= SIP_NOT_NONE;

                if (flags & FMT_AP_TRANSFER_THIS)
                    va_arg(va, PyObject **);

                if (flags & FMT_AP_NO_CONVERTORS)
                    iflgs |= SIP_NO_CONVERTORS;
                else
                    va_arg(va, int *);

                if (sipTypeNeedsUserState(sip_get_type_def(wms, type_id)))
                    va_arg(va, void **);

                if (arg != NULL && !can_convert_to_type(wms, arg, type_id, iflgs))
                    handle_failed_type_conversion(&failure, arg);

                break;
            }

        case 'N':
            {
                /* Python object of given type or None. */

                PyTypeObject *type = va_arg(va,PyTypeObject *);
                PyObject **p = va_arg(va,PyObject **);

                if (arg != NULL)
                {
                    if (arg == Py_None || PyObject_TypeCheck(arg,type))
                        *p = arg;
                    else
                        handle_failed_type_conversion(&failure, arg);
                }

                break;
            }

        case 'P':
            {
                /* Python object of any type with a sub-format. */

                va_arg(va, PyObject **);

                /* Skip the sub-format. */
                ++fmt;

                break;
            }

        case 'T':
            {
                /* Python object of given type. */

                PyTypeObject *type = va_arg(va, PyTypeObject *);
                PyObject **p = va_arg(va, PyObject **);

                if (arg != NULL)
                {
                    if (PyObject_TypeCheck(arg,type))
                        *p = arg;
                    else
                        handle_failed_type_conversion(&failure, arg);
                }

                break;
            }

        case 'F':
            {
                /* Python callable object. */
 
                PyObject **p = va_arg(va, PyObject **);

                if (arg != NULL)
                {
                    if (PyCallable_Check(arg))
                        *p = arg;
                    else
                        handle_failed_type_conversion(&failure, arg);
                }
 
                break;
            }

        case 'H':
            {
                /* Python callable object or None. */
 
                PyObject **p = va_arg(va, PyObject **);

                if (arg != NULL)
                {
                    if (arg == Py_None || PyCallable_Check(arg))
                        *p = arg;
                    else
                        handle_failed_type_conversion(&failure, arg);
                }
 
                break;
            }

        case '!':
            {
                /* Python object that implements the buffer protocol. */
 
                PyObject **p = va_arg(va, PyObject **);

                if (arg != NULL)
                {
                    if (PyObject_CheckBuffer(arg))
                        *p = arg;
                    else
                        handle_failed_type_conversion(&failure, arg);
                }
 
                break;
            }

        case '$':
            {
                /*
                 * Python object that implements the buffer protocol or None.
                 */
 
                PyObject **p = va_arg(va, PyObject **);

                if (arg != NULL)
                {
                    if (arg == Py_None || PyObject_CheckBuffer(arg))
                        *p = arg;
                    else
                        handle_failed_type_conversion(&failure, arg);
                }
 
                break;
            }

        case '&':
            {
                /* Python enum.Enum object. */
 
                PyObject **p = va_arg(va, PyObject **);

                if (arg != NULL)
                {
                    if (sip_enum_is_enum(sms, arg))
                        *p = arg;
                    else
                        handle_failed_type_conversion(&failure, arg);
                }
 
                break;
            }

        case '^':
            {
                /*
                 * Python enum.Enum object or None.
                 */
 
                PyObject **p = va_arg(va, PyObject **);

                if (arg != NULL)
                {
                    if (arg == Py_None || sip_enum_is_enum(wms->sip_module_state, arg))
                        *p = arg;
                    else
                        handle_failed_type_conversion(&failure, arg);
                }
 
                break;
            }

        case 'k':
            {
                /* Char array or None. */

                const char **p = va_arg(va, const char **);
                Py_ssize_t *szp = va_arg(va, Py_ssize_t *);

                if (arg != NULL)
                {
                    Py_ssize_t asize;
                    const char *cp = sip_api_bytes_as_char_array(arg, &asize);

                    if (PyErr_Occurred())
                    {
                        handle_failed_type_conversion(&failure, arg);
                    }
                    else
                    {
                        *p = cp;
                        *szp = asize;
                    }
                }

                break;
            }

        case 'K':
            {
                /* Wide char array or None. */

                PyObject **keep_p = va_arg(va, PyObject **);
                wchar_t **p = va_arg(va, wchar_t **);
                Py_ssize_t *szp = va_arg(va, Py_ssize_t *);

                if (arg != NULL)
                {
                    PyObject *keep = arg;
                    Py_ssize_t asize;

                    wchar_t *wcp = sip_api_string_as_wchar_array(&keep,
                            &asize);

                    if (PyErr_Occurred())
                    {
                        handle_failed_type_conversion(&failure, arg);
                    }
                    else
                    {
                        *keep_p = keep;
                        *p = wcp;
                        *szp = asize;
                    }
                }

                break;
            }

        case 'c':
            {
                /* Character from a Python bytes. */

                char *p = va_arg(va, char *);

                if (arg != NULL)
                {
                    char ch = sip_api_bytes_as_char(arg);

                    if (PyErr_Occurred())
                        handle_failed_type_conversion(&failure, arg);
                    else
                        *p = ch;
                }

                break;
            }

        case 'w':
            {
                /* Wide character. */

                wchar_t *p = va_arg(va, wchar_t *);

                if (arg != NULL)
                {
                    wchar_t wch = sip_api_string_as_wchar(arg);

                    if (PyErr_Occurred())
                        handle_failed_type_conversion(&failure, arg);
                    else
                        *p = wch;
                }

                break;
            }

        case 'b':
            {
                /* Bool. */

                _Bool *p = va_arg(va, _Bool *);

                if (arg != NULL)
                {
                    _Bool v = sip_api_convert_to_bool(arg);

                    if (PyErr_Occurred())
                        handle_failed_type_conversion(&failure, arg);
                    else
                        *p = v;
                }

                break;
            }

        case 'E':
            {
                /* Named or scoped enum. */

                sipTypeID type_id = va_arg(va, sipTypeID);
                int *p = va_arg(va, int *);

                if (arg != NULL)
                {
                    int v = sip_enum_convert_to_enum(wms, arg, type_id);

                    if (PyErr_Occurred())
                        handle_failed_type_conversion(&failure, arg);
                    else
                        *p = v;
                }
            }

            break;

        case 'e':
            {
                /* Anonymous enum. */

                int *p = va_arg(va, int *);

                if (arg != NULL)
                {
                    int v = sip_api_long_as_int(arg);

                    if (PyErr_Occurred())
                        handle_failed_int_conversion(&failure, arg);
                    else
                        *p = v;
                }
            }

            break;

        case 'i':
            {
                /* Integer. */

                int *p = va_arg(va, int *);

                if (arg != NULL)
                {
                    int v = sip_api_long_as_int(arg);

                    if (PyErr_Occurred())
                        handle_failed_int_conversion(&failure, arg);
                    else
                        *p = v;
                }

                break;
            }

        case 'u':
            {
                /* Unsigned integer. */

                unsigned *p = va_arg(va, unsigned *);

                if (arg != NULL)
                {
                    unsigned v = sip_api_long_as_unsigned_int(arg);

                    if (PyErr_Occurred())
                        handle_failed_int_conversion(&failure, arg);
                    else
                        *p = v;
                }

                break;
            }

        case '=':
            {
                /* size_t integer. */

                size_t *p = va_arg(va, size_t *);

                if (arg != NULL)
                {
                    size_t v = sip_api_long_as_size_t(arg);

                    if (PyErr_Occurred())
                        handle_failed_int_conversion(&failure, arg);
                    else
                        *p = v;
                }

                break;
            }

        case 'I':
            {
                /* Char as an integer. */

                char *p = va_arg(va, char *);

                if (arg != NULL)
                {
                    char v = sip_api_long_as_char(arg);

                    if (PyErr_Occurred())
                        handle_failed_int_conversion(&failure, arg);
                    else
                        *p = v;
                }

                break;
            }

        case 'L':
            {
                /* Signed char as an integer. */

                signed char *p = va_arg(va, signed char *);

                if (arg != NULL)
                {
                    signed char v = sip_api_long_as_signed_char(arg);

                    if (PyErr_Occurred())
                        handle_failed_int_conversion(&failure, arg);
                    else
                        *p = v;
                }

                break;
            }

        case 'M':
            {
                /* Unsigned char as an integer. */

                unsigned char *p = va_arg(va, unsigned char *);

                if (arg != NULL)
                {
                    unsigned char v = sip_api_long_as_unsigned_char(arg);

                    if (PyErr_Occurred())
                        handle_failed_int_conversion(&failure, arg);
                    else
                        *p = v;
                }

                break;
            }

        case 'h':
            {
                /* Short integer. */

                signed short *p = va_arg(va, signed short *);

                if (arg != NULL)
                {
                    signed short v = sip_api_long_as_short(arg);

                    if (PyErr_Occurred())
                        handle_failed_int_conversion(&failure, arg);
                    else
                        *p = v;
                }

                break;
            }

        case 't':
            {
                /* Unsigned short integer. */

                unsigned short *p = va_arg(va, unsigned short *);

                if (arg != NULL)
                {
                    unsigned short v = sip_api_long_as_unsigned_short(arg);

                    if (PyErr_Occurred())
                        handle_failed_int_conversion(&failure, arg);
                    else
                        *p = v;
                }

                break;
            }

        case 'l':
            {
                /* Long integer. */

                long *p = va_arg(va, long *);

                if (arg != NULL)
                {
                    long v = sip_api_long_as_long(arg);

                    if (PyErr_Occurred())
                        handle_failed_int_conversion(&failure, arg);
                    else
                        *p = v;
                }

                break;
            }

        case 'm':
            {
                /* Unsigned long integer. */

                unsigned long *p = va_arg(va, unsigned long *);

                if (arg != NULL)
                {
                    unsigned long v = sip_api_long_as_unsigned_long(arg);

                    if (PyErr_Occurred())
                        handle_failed_int_conversion(&failure, arg);
                    else
                        *p = v;
                }

                break;
            }

        case 'n':
            {
                /* Long long integer. */

                long long *p = va_arg(va, long long *);

                if (arg != NULL)
                {
                    long long v = sip_api_long_as_long_long(arg);

                    if (PyErr_Occurred())
                        handle_failed_int_conversion(&failure, arg);
                    else
                        *p = v;
                }

                break;
            }

        case 'o':
            {
                /* Unsigned long long integer. */

                unsigned long long *p = va_arg(va, unsigned long long *);

                if (arg != NULL)
                {
                    unsigned long long v = sip_api_long_as_unsigned_long_long(arg);

                    if (PyErr_Occurred())
                        handle_failed_int_conversion(&failure, arg);
                    else
                        *p = v;
                }

                break;
            }

        case 'f':
            {
                /* Float. */

                float *p = va_arg(va, float *);

                if (arg != NULL)
                {
                    double v = PyFloat_AsDouble(arg);

                    if (PyErr_Occurred())
                        handle_failed_type_conversion(&failure, arg);
                    else
                        *p = (float)v;
                }

                break;
            }

        case 'X':
            {
                /* Constrained types. */

                char sub_fmt = *fmt++;

                if (sub_fmt == 'E')
                {
                    sipTypeID type_id = va_arg(va, sipTypeID);
                    int *p = va_arg(va, int *);

                    if (arg != NULL)
                    {
                        *p = sip_enum_convert_to_constrained_enum(wms, arg,
                                type_id);

                        if (PyErr_Occurred())
                            handle_failed_type_conversion(&failure, arg);
                    }
                }
                else
                {
                    void *p = va_arg(va, void *);

                    if (arg != NULL)
                    {
                        switch (sub_fmt)
                        {
                        case 'b':
                            {
                                /* Boolean. */

                                if (PyBool_Check(arg))
                                    *(_Bool *)p = (arg == Py_True);
                                else
                                    handle_failed_type_conversion(&failure,
                                            arg);

                                break;
                            }

                        case 'd':
                            {
                                /* Double float. */

                                if (PyFloat_Check(arg))
                                    *(double *)p = PyFloat_AS_DOUBLE(arg);
                                else
                                    handle_failed_type_conversion(&failure,
                                            arg);

                                break;
                            }

                        case 'f':
                            {
                                /* Float. */

                                if (PyFloat_Check(arg))
                                    *(float *)p = (float)PyFloat_AS_DOUBLE(arg);
                                else
                                    handle_failed_type_conversion(&failure,
                                            arg);

                                break;
                            }

                        case 'i':
                            {
                                /* Integer. */

                                if (PyLong_Check(arg))
                                {
                                    *(int *)p = sip_api_long_as_int(arg);

                                    if (PyErr_Occurred())
                                        handle_failed_int_conversion(&failure,
                                                arg);
                                }
                                else
                                {
                                        handle_failed_type_conversion(&failure,
                                                arg);
                                }

                                break;
                            }
                        }
                    }
                }

                break;
            }

        case 'd':
            {
                /* Double float. */

                double *p = va_arg(va,double *);

                if (arg != NULL)
                {
                    double v = PyFloat_AsDouble(arg);

                    if (PyErr_Occurred())
                        handle_failed_type_conversion(&failure, arg);
                    else
                        *p = v;
                }

                break;
            }

        case 'v':
            {
                /* Void pointer. */

                void **p = va_arg(va, void **);

                if (arg != NULL)
                {
                    void *v = sip_api_convert_to_void_ptr(arg);

                    if (PyErr_Occurred())
                        handle_failed_type_conversion(&failure, arg);
                    else
                        *p = v;
                }

                break;
            }

        case 'z':
            {
                /* Void pointer as a capsule. */

                const char *name = va_arg(va, const char *);
                void **p = va_arg(va, void **);

                if (arg == Py_None)
                {
                    *p = NULL;
                }
                else if (arg != NULL)
                {
                    void *v = PyCapsule_GetPointer(arg, name);

                    if (PyErr_Occurred())
                        handle_failed_type_conversion(&failure, arg);
                    else
                        *p = v;
                }

                break;
            }
        }

        if ((failure.reason == Ok || failure.reason == Overflow) && ch == 'W')
        {
            /* An ellipsis matches everything and ends the parse. */
            break;
        }
    }

    /* Handle parse failures appropriately. */

    if (failure.reason == Ok)
        return TRUE;

    if (failure.reason == Overflow)
    {
        /*
         * We have successfully parsed the signature but one of the arguments
         * has been found to overflow.  Raise an appropriate exception and
         * ensure we don't parse any subsequent overloads.
         */
        if (failure.overflow_arg_nr >= 0)
        {
            PyErr_Format(PyExc_OverflowError, "argument %d overflowed: %S",
                    failure.overflow_arg_nr, failure.detail_obj);
        }
        else
        {
            PyErr_Format(PyExc_OverflowError, "argument '%s' overflowed: %S",
                    failure.overflow_arg_name, failure.detail_obj);
        }

        /* The overflow exception has now been raised. */
        failure.reason = Raised;
    }

    if (failure.reason != Raised)
        add_failure(parse_err_p, &failure);

    if (failure.reason == Raised)
    {
        Py_XDECREF(failure.detail_obj);

        /*
         * Discard any previous errors and flag that the exception we want the
         * user to see has been raised.
         */
        Py_XDECREF(*parse_err_p);
        *parse_err_p = Py_None;
        Py_INCREF(Py_None);
    }

    return FALSE;
}


/*
 * Second pass of the argument parse, converting the remaining ones that might
 * have side effects.  Return TRUE if there was no error.
 */
static int parse_pass_2(sipWrappedModuleState *wms, PyObject *self,
        int self_in_args, PyObject *const *args, Py_ssize_t nr_args,
        PyObject *kwd_names, Py_ssize_t nr_kwd_names, const char **kwd_list,
        const char *fmt, va_list va)
{
    /* Handle the conversions of "self" first. */
    int isstatic = FALSE;

    switch (*fmt++)
    {
    case '#':
        va_arg(va, PyObject *);
        break;

    case 'B':
        {
            /*
             * The address of a C++ instance when calling one of its public
             * methods.
             */

            *va_arg(va, PyObject **) = self;
            sipTypeID type_id = va_arg(va, sipTypeID);
            void **p = va_arg(va, void **);

            if ((*p = sip_get_cpp_ptr(wms, (sipSimpleWrapper *)self, type_id)) == NULL)
                return FALSE;

            break;
        }

    case 'p':
        {
            /*
             * The address of a C++ instance when calling one of its protected
             * methods.
             */

            *va_arg(va, PyObject **) = self;
            sipTypeID type_id = va_arg(va, sipTypeID);
            void **p = va_arg(va, void **);

            if ((*p = sip_get_complex_cpp_ptr(wms, (sipSimpleWrapper *)self, type_id)) == NULL)
                return FALSE;

            break;
        }

    case 'C':
        *va_arg(va, PyObject **) = self;
        isstatic = TRUE;
        break;

    default:
        --fmt;
    }

    Py_ssize_t arg_nr;
    int ok = TRUE;

    for (arg_nr = (self_in_args ? 1 : 0); *fmt != '\0' && *fmt != 'W' && ok; arg_nr++)
    {
        char ch;
        PyObject *arg;

        /* Skip the optional character. */
        if ((ch = *fmt++) == '|')
            ch = *fmt++;

        /* Get the next argument. */
        arg = NULL;

        if (arg_nr < nr_args)
        {
            arg = args[arg_nr];
        }
        else if (kwd_names != NULL)
        {
            const char *name = kwd_list[arg_nr - self_in_args];

            if (name != NULL)
                arg = get_kwd_arg(args, nr_args, kwd_names, nr_kwd_names,
                        name);
        }

        assert(arg != NULL);

        /*
         * Do the outstanding conversions.  For most types it has already been
         * done, so we are just skipping the parameters.
         */
        switch (ch)
        {
        case '@':
            /* Implement /GetWrapper/. */
            va_arg(va, PyObject **);

            /* Process the same argument next time round. */
            --arg_nr;

            break;

        case 'r':
            {
                /* Sequence of mapped type instances. */

                sipTypeID type_id = va_arg(va, sipTypeID);
                void **array = va_arg(va, void **);
                Py_ssize_t *nr_elem = va_arg(va, Py_ssize_t *);

                if (arg != NULL && !convert_from_sequence(wms, arg, type_id, array, nr_elem))
                    return FALSE;

                break;
            }

        case '>':
            {
                /* Sequence or sip.array of class instances. */

                sipTypeID type_id = va_arg(va, sipTypeID);
                void **array = va_arg(va, void **);
                Py_ssize_t *nr_elem = va_arg(va, Py_ssize_t *);
                int *is_temp = va_arg(va, int *);

                if (arg != NULL)
                {
                    if (sip_array_can_convert(wms, arg, type_id))
                    {
                        sip_array_convert(arg, array, nr_elem);
                        *is_temp = FALSE;
                    }
                    else if (convert_from_sequence(wms, arg, type_id, array, nr_elem))
                    {
                        /*
                         * Note that this will leak if there is a subsequent
                         * error.
                         */
                        *is_temp = TRUE;
                    }
                    else
                    {
                        return FALSE;
                    }
                }

                break;
            }

        case 'J':
            {
                /* Class or mapped type instance. */

                int flags = *fmt++ - '0';
                int iflgs = 0;
                int *statep;
                PyObject *xfer, **owner;
                void **user_statep;

                sipTypeID type_id = va_arg(va, sipTypeID);
                void **p = va_arg(va, void **);

                if (flags & FMT_AP_TRANSFER)
                    xfer = ((isstatic || self == NULL) ? arg : self);
                else if (flags & FMT_AP_TRANSFER_BACK)
                    xfer = Py_None;
                else
                    xfer = NULL;

                if (flags & FMT_AP_DEREF)
                    iflgs |= SIP_NOT_NONE;

                if (flags & FMT_AP_TRANSFER_THIS)
                    owner = va_arg(va, PyObject **);
                else
                    owner = NULL;

                if (flags & FMT_AP_NO_CONVERTORS)
                {
                    iflgs |= SIP_NO_CONVERTORS;
                    statep = NULL;
                }
                else
                {
                    statep = va_arg(va, int *);
                }

                if (sipTypeNeedsUserState(sip_get_type_def(wms, type_id)))
                    user_statep = va_arg(va, void **);
                else
                    user_statep = NULL;

                if (arg != NULL)
                {
                    int iserr = FALSE;

                    *p = convert_to_type_us(wms, arg, type_id, xfer, iflgs,
                            statep, user_statep, &iserr);

                    if (iserr)
                        return FALSE;

                    if (owner != NULL && *p != NULL)
                        *owner = arg;
                }

                break;
            }

        case 'P':
            {
                /* Python object of any type with a sub-format. */

                PyObject **p = va_arg(va, PyObject **);
                int flags = *fmt++ - '0';

                if (arg != NULL)
                {
                    if (flags & FMT_AP_TRANSFER)
                    {
                        Py_XINCREF(arg);
                    }
                    else if (flags & FMT_AP_TRANSFER_BACK)
                    {
                        Py_XDECREF(arg);
                    }

                    *p = arg;
                }

                break;
            }

        case 'X':
            {
                /* Constrained types. */

                if (*fmt++ == 'E')
                    va_arg(va, void *);

                va_arg(va, void *);

                break;
            }

        case 'A':
            {
                /* String from a Python string or None. */

                PyObject **keep_p = va_arg(va, PyObject **);
                const char **p = va_arg(va, const char **);
                char sub_fmt = *fmt++;

                if (arg != NULL)
                {
                    PyObject *keep = arg;
                    const char *cp;

                    switch (sub_fmt)
                    {
                    case 'A':
                        cp = sip_api_string_as_ascii_string(&keep);
                        break;

                    case 'L':
                        cp = sip_api_string_as_latin1_string(&keep);
                        break;

                    case '8':
                        cp = sip_api_string_as_utf8_string(&keep);
                        break;
                    }

                    if (PyErr_Occurred())
                        return FALSE;

                    *keep_p = keep;
                    *p = cp;
                }

                break;
            }

        case 'a':
            {
                /* Character from a Python string. */

                char *p = va_arg(va, char *);
                char sub_fmt = *fmt++;

                if (arg != NULL)
                {
                    char ch;

                    switch (sub_fmt)
                    {
                    case 'A':
                        ch = sip_api_string_as_ascii_char(arg);
                        break;

                    case 'L':
                        ch = sip_api_string_as_latin1_char(arg);
                        break;

                    case '8':
                        ch = sip_api_string_as_utf8_char(arg);
                        break;
                    }

                    if (PyErr_Occurred())
                        return FALSE;

                    *p = ch;
                }

                break;
            }

        /*
         * Every other argument is a pointer and only differ in how many there
         * are.
         */
        case 'N':
        case 'T':
        case 'k':
        case 'K':
        case 'U':
        case 'E':
            va_arg(va, void *);

            /* Drop through. */

        default:
            va_arg(va, void *);
        }
    }

    /* Handle any ellipsis argument. */
    if (*fmt == 'W')
    {
        PyObject *al;
        int da = 0;

        /* Create a tuple for any remaining arguments. */
        if ((al = PyTuple_New(nr_args - arg_nr)) == NULL)
            return FALSE;

        while (arg_nr < nr_args)
        {
            PyObject *arg = args[arg_nr];

            /* Add the remaining argument to the tuple. */
            Py_INCREF(arg);
            PyTuple_SET_ITEM(al, da, arg);

            ++arg_nr;
            ++da;
        }

        /* Return the tuple. */
        *va_arg(va, PyObject **) = al;
    }

    return TRUE;
}


/*
 * Do the main work of parsing a result object based on a format string.
 */
static int parse_result(sipWrappedModuleState *wms, PyObject *method,
        PyObject *res, sipSimpleWrapper *py_self, const char *fmt, va_list va)
{
    /* We rely on PyErr_Occurred(). */
    PyErr_Clear();

    /* Get self if it is provided as an argument. */
    if (*fmt == 'S')
    {
        py_self = va_arg(va, sipSimpleWrapper *);
        ++fmt;
    }

    /* Basic validation of the format string. */
    Py_ssize_t tupsz;
    int rc = 0;

    if (*fmt == '(')
    {
        char ch;
        const char *cp = ++fmt;
        int sub_format = FALSE;

        tupsz = 0;

        while ((ch = *cp++) != ')')
        {
            if (ch == '\0')
            {
                PyErr_Format(PyExc_SystemError,
                        "sipParseResult(): invalid format string \"%s\"",
                        fmt - 1);
                rc = -1;

                break;
            }

            if (sub_format)
            {
                sub_format = FALSE;
            }
            else
            {
                tupsz++;

                /* Some format characters have a sub-format. */
                if (strchr("aAHDC", ch) != NULL)
                    sub_format = TRUE;
            }
        }

        if (rc == 0)
            if (!PyTuple_Check(res) || PyTuple_GET_SIZE(res) != tupsz)
            {
                sip_api_bad_catcher_result(method);
                rc = -1;
            }
    }
    else
    {
        tupsz = -1;
    }

    if (rc == 0)
    {
        char ch;
        int i = 0;

        while ((ch = *fmt++) != '\0' && ch != ')' && rc == 0)
        {
            PyObject *arg;
            int invalid = FALSE;

            if (tupsz > 0)
            {
                arg = PyTuple_GET_ITEM(res, i);
                i++;
            }
            else
            {
                arg = res;
            }

            switch (ch)
            {
            case 'g':
                {
                    const char **p = va_arg(va, const char **);
                    Py_ssize_t *szp = va_arg(va, Py_ssize_t *);

                    Py_ssize_t asize;
                    const char *cp = sip_api_bytes_as_char_array(arg, &asize);

                    if (PyErr_Occurred())
                    {
                        invalid = TRUE;
                    }
                    else
                    {
                        *p = cp;
                        *szp = asize;
                    }
                }

                break;

            case 'G':
                {
                    int key = va_arg(va, int);
                    wchar_t **p = va_arg(va, wchar_t **);
                    Py_ssize_t *szp = va_arg(va, Py_ssize_t *);

                    PyObject *keep = arg;
                    Py_ssize_t asize;

                    wchar_t *wcp = sip_api_string_as_wchar_array(&keep,
                            &asize);

                    if (PyErr_Occurred() || sip_keep_reference(wms, py_self, key, keep) < 0)
                    {
                        invalid = TRUE;
                    }
                    else
                    {
                        *p = wcp;
                        *szp = asize;
                    }
                }

                break;

            case 'b':
                {
                    _Bool *p = va_arg(va, _Bool *);

                    _Bool v = sip_api_convert_to_bool(arg);

                    if (PyErr_Occurred())
                        invalid = TRUE;
                    else if (p != NULL)
                        *p = v;
                }

                break;

            case 'c':
                {
                    char *p = va_arg(va, char *);

                    char ch = sip_api_bytes_as_char(arg);

                    if (PyErr_Occurred())
                        invalid = TRUE;
                    else
                        *p = ch;
                }

                break;

            case 'a':
                {
                    char *p = va_arg(va, char *);
                    char ch;

                    switch (*fmt++)
                    {
                    case 'A':
                        ch = sip_api_string_as_ascii_char(arg);
                        break;

                    case 'L':
                        ch = sip_api_string_as_latin1_char(arg);
                        break;

                    case '8':
                        ch = sip_api_string_as_utf8_char(arg);
                        break;
                    }

                    if (PyErr_Occurred())
                        invalid = TRUE;
                    else
                        *p = ch;
                }

                break;

            case 'w':
                {
                    wchar_t *p = va_arg(va, wchar_t *);

                    wchar_t wch = sip_api_string_as_wchar(arg);

                    if (PyErr_Occurred())
                        invalid = TRUE;
                    else
                        *p = wch;
                }

                break;

            case 'd':
                {
                    double *p = va_arg(va, double *);
                    double v = PyFloat_AsDouble(arg);

                    if (PyErr_Occurred())
                        invalid = TRUE;
                    else if (p != NULL)
                        *p = v;
                }

                break;

            case 'F':
                {
                    sipTypeID type_id = va_arg(va, sipTypeID);
                    int *p = va_arg(va, int *);
                    int v = sip_enum_convert_to_enum(wms, arg, type_id);

                    if (PyErr_Occurred())
                        invalid = TRUE;
                    else if (p != NULL)
                        *p = v;
                }

                break;

            case 'f':
                {
                    float *p = va_arg(va, float *);
                    float v = (float)PyFloat_AsDouble(arg);

                    if (PyErr_Occurred())
                        invalid = TRUE;
                    else if (p != NULL)
                        *p = v;
                }

                break;

            case 'I':
                {
                    char *p = va_arg(va, char *);
                    char v = sip_api_long_as_char(arg);

                    if (PyErr_Occurred())
                        invalid = TRUE;
                    else if (p != NULL)
                        *p = v;
                }

                break;

            case 'L':
                {
                    signed char *p = va_arg(va, signed char *);
                    signed char v = sip_api_long_as_signed_char(arg);

                    if (PyErr_Occurred())
                        invalid = TRUE;
                    else if (p != NULL)
                        *p = v;
                }

                break;

            case 'M':
                {
                    unsigned char *p = va_arg(va, unsigned char *);
                    unsigned char v = sip_api_long_as_unsigned_char(arg);

                    if (PyErr_Occurred())
                        invalid = TRUE;
                    else if (p != NULL)
                        *p = v;
                }

                break;

            case 'h':
                {
                    signed short *p = va_arg(va, signed short *);
                    signed short v = sip_api_long_as_short(arg);

                    if (PyErr_Occurred())
                        invalid = TRUE;
                    else if (p != NULL)
                        *p = v;
                }

                break;

            case 't':
                {
                    unsigned short *p = va_arg(va, unsigned short *);
                    unsigned short v = sip_api_long_as_unsigned_short(arg);

                    if (PyErr_Occurred())
                        invalid = TRUE;
                    else if (p != NULL)
                        *p = v;
                }

                break;

            case 'e':
                {
                    int *p = va_arg(va, int *);
                    int v = sip_api_long_as_int(arg);

                    if (PyErr_Occurred())
                        invalid = TRUE;
                    else if (p != NULL)
                        *p = v;
                }

                break;

            case 'i':
                {
                    int *p = va_arg(va, int *);
                    int v = sip_api_long_as_int(arg);

                    if (PyErr_Occurred())
                        invalid = TRUE;
                    else if (p != NULL)
                        *p = v;
                }

                break;

            case 'u':
                {
                    unsigned *p = va_arg(va, unsigned *);
                    unsigned v = sip_api_long_as_unsigned_int(arg);

                    if (PyErr_Occurred())
                        invalid = TRUE;
                    else if (p != NULL)
                        *p = v;
                }

                break;

            case '=':
                {
                    size_t *p = va_arg(va, size_t *);
                    size_t v = sip_api_long_as_size_t(arg);

                    if (PyErr_Occurred())
                        invalid = TRUE;
                    else if (p != NULL)
                        *p = v;
                }

                break;

            case 'l':
                {
                    long *p = va_arg(va, long *);
                    long v = sip_api_long_as_long(arg);

                    if (PyErr_Occurred())
                        invalid = TRUE;
                    else if (p != NULL)
                        *p = v;
                }

                break;

            case 'm':
                {
                    unsigned long *p = va_arg(va, unsigned long *);
                    unsigned long v = sip_api_long_as_unsigned_long(arg);

                    if (PyErr_Occurred())
                        invalid = TRUE;
                    else if (p != NULL)
                        *p = v;
                }

                break;

            case 'n':
                {
                    long long *p = va_arg(va, long long *);
                    long long v = sip_api_long_as_long_long(arg);

                    if (PyErr_Occurred())
                        invalid = TRUE;
                    else if (p != NULL)
                        *p = v;
                }

                break;

            case 'o':
                {
                    unsigned long long *p = va_arg(va, unsigned long long *);
                    unsigned long long v = sip_api_long_as_unsigned_long_long(arg);

                    if (PyErr_Occurred())
                        invalid = TRUE;
                    else if (p != NULL)
                        *p = v;
                }

                break;

            case 'A':
                {
                    int key = va_arg(va, int);
                    const char **p = va_arg(va, const char **);

                    PyObject *keep = arg;
                    const char *cp;

                    switch (*fmt++)
                    {
                    case 'A':
                        cp = sip_api_string_as_ascii_string(&keep);
                        break;

                    case 'L':
                        cp = sip_api_string_as_latin1_string(&keep);
                        break;

                    case '8':
                        cp = sip_api_string_as_utf8_string(&keep);
                        break;
                    }

                    if (PyErr_Occurred() || sip_keep_reference(wms, py_self, key, keep) < 0)
                        invalid = TRUE;
                    else
                        *p = cp;
                }

                break;

            case 'B':
                {
                    int key = va_arg(va, int);
                    const char **p = va_arg(va, const char **);

                    const char *cp = sip_api_bytes_as_string(arg);

                    if (PyErr_Occurred() || sip_keep_reference(wms, py_self, key, arg) < 0)
                        invalid = TRUE;
                    else
                        *p = cp;
                }

                break;

            case 'x':
                {
                    int key = va_arg(va, int);
                    wchar_t **p = va_arg(va, wchar_t **);

                    PyObject *keep = arg;

                    wchar_t *wcp = sip_api_string_as_wstring(&keep);

                    if (PyErr_Occurred() || sip_keep_reference(wms, py_self, key, keep) < 0)
                        invalid = TRUE;
                    else
                        *p = wcp;
                }

                break;

            case 'H':
                {
                    if (*fmt == '\0')
                    {
                        invalid = TRUE;
                    }
                    else
                    {
                        sipTypeID type_id = va_arg(va, sipTypeID);
                        void *cpp = va_arg(va, void **);

                        int flags = *fmt++ - '0';
                        int iserr = FALSE, state;
                        void *user_state;

                        void *val = sip_force_convert_to_type_us(wms, arg,
                                type_id,
                                (flags & FMT_RP_FACTORY ? arg : NULL),
                                (flags & FMT_RP_DEREF ? SIP_NOT_NONE : 0),
                                &state,
                                (flags & FMT_RP_MAKE_COPY ? &user_state : NULL),
                                &iserr);

                        if (iserr)
                        {
                            invalid = TRUE;
                        }
                        else if (flags & FMT_RP_MAKE_COPY)
                        {
                            const sipTypeDef *td = sip_get_type_def(wms,
                                    type_id);

                            sipAssignFunc assign_helper;

                            if (sipTypeIsMapped(td))
                                assign_helper = ((const sipMappedTypeDef *)td)->mtd_assign;
                            else
                                assign_helper = ((const sipClassTypeDef *)td)->ctd_assign;

                            assert(assign_helper != NULL);

                            if (cpp != NULL)
                                assign_helper(cpp, 0, val);

                            release_type_us(wms, val, type_id, state,
                                    user_state);
                        }
                        else if (cpp != NULL)
                        {
                            *(void **)cpp = val;
                        }
                    }
                }

                break;

            case 'N':
                {
                    PyTypeObject *type = va_arg(va, PyTypeObject *);
                    PyObject **p = va_arg(va, PyObject **);

                    if (arg == Py_None || PyObject_TypeCheck(arg, type))
                    {
                        if (p != NULL)
                        {
                            Py_INCREF(arg);
                            *p = arg;
                        }
                    }
                    else
                    {
                        invalid = TRUE;
                    }
                }

                break;

            case 'O':
                {
                    PyObject **p = va_arg(va, PyObject **);

                    if (p != NULL)
                    {
                        Py_INCREF(arg);
                        *p = arg;
                    }
                }

                break;

            case 'T':
                {
                    PyTypeObject *type = va_arg(va, PyTypeObject *);
                    PyObject **p = va_arg(va, PyObject **);

                    if (PyObject_TypeCheck(arg, type))
                    {
                        if (p != NULL)
                        {
                            Py_INCREF(arg);
                            *p = arg;
                        }
                    }
                    else
                    {
                        invalid = TRUE;
                    }
                }

                break;

            case 'V':
                {
                    void *v = sip_api_convert_to_void_ptr(arg);
                    void **p = va_arg(va, void **);

                    if (PyErr_Occurred())
                        invalid = TRUE;
                    else if (p != NULL)
                        *p = v;
                }

                break;

            case 'z':
                {
                    const char *name = va_arg(va, const char *);
                    void **p = va_arg(va, void **);

                    if (arg == Py_None)
                    {
                        if (p != NULL)
                            *p = NULL;
                    }
                    else
                    {
                        void *v = PyCapsule_GetPointer(arg, name);

                        if (PyErr_Occurred())
                            invalid = TRUE;
                        else if (p != NULL)
                            *p = v;
                    }
                }

                break;

            case 'Z':
                if (arg != Py_None)
                    invalid = TRUE;

                break;

            case '!':
                {
                    PyObject **p = va_arg(va, PyObject **);

                    if (PyObject_CheckBuffer(arg))
                    {
                        if (p != NULL)
                        {
                            Py_INCREF(arg);
                            *p = arg;
                        }
                    }
                    else
                    {
                        invalid = TRUE;
                    }
                }

                break;

            case '$':
                {
                    PyObject **p = va_arg(va, PyObject **);

                    if (arg == Py_None || PyObject_CheckBuffer(arg))
                    {
                        if (p != NULL)
                        {
                            Py_INCREF(arg);
                            *p = arg;
                        }
                    }
                    else
                    {
                        invalid = TRUE;
                    }
                }

                break;

            case '&':
                {
                    PyObject **p = va_arg(va, PyObject **);

                    if (sip_enum_is_enum(wms->sip_module_state, arg))
                    {
                        if (p != NULL)
                        {
                            Py_INCREF(arg);
                            *p = arg;
                        }
                    }
                    else
                    {
                        invalid = TRUE;
                    }
                }

                break;

            case '^':
                {
                    PyObject **p = va_arg(va, PyObject **);

                    if (arg == Py_None || sip_enum_is_enum(wms->sip_module_state, arg))
                    {
                        if (p != NULL)
                        {
                            Py_INCREF(arg);
                            *p = arg;
                        }
                    }
                    else
                    {
                        invalid = TRUE;
                    }
                }

                break;

            default:
                PyErr_Format(PyExc_SystemError,
                        "sipParseResult(): invalid format character '%c'", ch);
                rc = -1;
            }

            if (invalid)
            {
                sip_api_bad_catcher_result(method);
                rc = -1;
                break;
            }
        }
    }

    return rc;
}


/*
 * Raise an exception when there is no mapped type converter to convert to
 * C/C++ from Python.
 */
static void raise_no_convert_to(PyObject *py, const sipTypeDef *td)
{
    PyErr_Format(PyExc_TypeError, "%s cannot be converted to %s",
            Py_TYPE(py)->tp_name, td->td_cname);
}


/*
 * Implement the release of a possibly temporary C/C++ instance created by a
 * type convertor.
 */
static void release_type_us(sipWrappedModuleState *wms, void *cpp,
        sipTypeID type_id, int state, void *user_state)
{
    /* See if there is something to release. */
    if (state & SIP_TEMPORARY)
        sip_release(cpp, sip_get_type_def(wms, type_id), state, user_state);
}


/*
 * Return a string/unicode object extracted from a particular line of a
 * docstring.
 */
static PyObject *signature_from_docstring(const char *doc, Py_ssize_t line)
{
    const char *eol;
    Py_ssize_t size = 0;

    /*
     * Find the start of the line.  If there is a non-default versioned
     * overload that has been enabled then it won't have an entry in the
     * docstring.  This means that the returned signature may be incorrect.
     */
    while (line-- > 0)
    {
        const char *next = strchr(doc, '\n');

        if (next == NULL)
            break;

        doc = next + 1;
    }

    /* Find the last closing parenthesis. */
    for (eol = doc; *eol != '\n' && *eol != '\0'; ++eol)
        if (*eol == ')')
            size = eol - doc + 1;

    return PyUnicode_FromStringAndSize(doc, size);
}


/*
 * Check that a user state pointer has been provided if the type requires it.
 * This is most likely a problem with handwritten code.
 */
static int user_state_is_valid(const sipTypeDef *td, void **user_statep)
{
    if (sipTypeNeedsUserState(td) && user_statep == NULL)
    {
        PyErr_Format(PyExc_RuntimeError,
                "%s requires user state but none is provided",
                td->td_cname);

        return FALSE;
    }

    return TRUE;
}
