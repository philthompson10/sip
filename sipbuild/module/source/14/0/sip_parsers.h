/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the argument parsers support.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_PARSERS_H
#define _SIP_PARSERS_H


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip.h"


#ifdef __cplusplus
extern "C" {
#endif


void sip_api_add_exception(sipErrorState es, PyObject **parseErrp);
sipErrorState sip_api_bad_callable_arg(int arg_nr, PyObject *arg);
void sip_api_bad_catcher_result(PyObject *method);
PyObject *sip_api_build_result(PyObject *wmod, int *is_err_p, const char *fmt,
        ...);
void sip_api_call_error_handler(sipVirtErrorHandlerFunc error_handler,
        sipSimpleWrapper *py_self, sip_gilstate_t gil_state);
PyObject *sip_api_call_method(PyObject *wmod, int *is_err_p, PyObject *method,
        const char *fmt, ...);
void sip_api_call_procedure_method(PyObject *wmod, sip_gilstate_t gil_state,
        sipVirtErrorHandlerFunc error_handler, sipSimpleWrapper *py_self,
        PyObject *method, const char *fmt, ...);
int sip_api_can_convert_to_type(PyObject *wmod, PyObject *pyObj,
        sipTypeID type_id, int flags);
PyObject *sip_api_convert_from_new_pytype(PyObject *wmod, void *cpp,
        PyTypeObject *py_type, sipWrapper *owner, sipSimpleWrapper **self_p,
        const char *fmt, ...);
PyObject *sip_api_convert_from_new_type(PyObject *wmod, void *cpp,
        sipTypeID type_id, PyObject *transferObj);
PyObject *sip_api_convert_from_type(PyObject *wmod, void *cpp,
        sipTypeID type_id, PyObject *transferObj);
void *sip_api_convert_to_type(PyObject *wmod, PyObject *pyObj,
        sipTypeID type_id, PyObject *transferObj, int flags, int *statep,
        int *iserrp);
void *sip_api_convert_to_type_us(PyObject *wmod, PyObject *pyObj,
        sipTypeID type_id, PyObject *transferObj, int flags, int *statep,
        void **user_statep, int *iserrp);
void *sip_api_force_convert_to_type(PyObject *wmod, PyObject *pyObj,
        sipTypeID type_id, PyObject *transferObj, int flags, int *statep,
        int *iserrp);
void *sip_api_force_convert_to_type_us(PyObject *wmod, PyObject *pyObj,
        sipTypeID type_id, PyObject *transferObj, int flags, int *statep,
        void **user_statep, int *iserrp);
PyObject *sip_api_get_pyobject(PyObject *wmod, void *cppPtr,
        sipTypeID type_id);
void sip_api_no_function(PyObject *parse_err, const char *func,
        const char *doc);
void sip_api_no_method(PyObject *parse_err, const char *scope,
        const char *method, const char *doc);
int sip_api_parse_args(PyObject *wmod, PyObject **parse_err_p,
        PyObject *const *args, Py_ssize_t nr_args, const char *fmt, ...);
int sip_api_parse_kwd_args(PyObject *wmod, PyObject **parse_err_p,
        PyObject *const *args, Py_ssize_t nr_args, PyObject *kwd_names,
        const char **kwd_list, PyObject **unused, const char *fmt, ...);
int sip_api_parse_pair(PyObject *wmod, PyObject **parse_err_p, PyObject *arg_0,
        PyObject *arg_1, const char *fmt, ...);
int sip_api_parse_result(PyObject *wmod, sip_gilstate_t gil_state,
        sipVirtErrorHandlerFunc error_handler, sipSimpleWrapper *py_self,
        PyObject *method, PyObject *res, const char *fmt, ...);
void sip_api_release_type(PyObject *wmod, void *cpp, sipTypeID type_id,
        int state);
void sip_api_release_type_us(PyObject *wmod, void *cpp, sipTypeID type_id,
        int state, void *user_state);

PyObject *sip_convert_from_type(sipWrappedModuleState *sms, void *cppPtr,
        sipTypeID type_id, PyObject *transferObj);
void *sip_force_convert_to_type_us(sipWrappedModuleState *wms, PyObject *pyObj,
        sipTypeID type_id, PyObject *transferObj, int flags, int *statep,
        void **user_statep, int *iserrp);
PyObject *sip_is_py_method(sipWrappedModuleState *wms, sip_gilstate_t *gil,
        char *pymc, sipSimpleWrapper **sipSelfp, const char *cname,
        const char *mname);
void sip_release(void *addr, const sipTypeDef *td, int state,
        void *user_state);


#ifdef __cplusplus
}
#endif

#endif
