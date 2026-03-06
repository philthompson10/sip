/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the argument parsers support.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_PARSERS_H
#define _SIP_PARSERS_H

#include <Python.h>

#include "sip.h"
#include "sip_decls.h"


#ifdef __cplusplus
extern "C" {
#endif

void sip_api_add_exception(sipErrorState es, PyObject **parseErrp);
sipErrorState sip_api_bad_callable_arg(int arg_nr, PyObject *arg);
void sip_api_bad_catcher_result(PyObject *method);
PyObject *sip_api_build_result(PyObject *mod, int *is_err_p, const char *fmt,
        ...);
void sip_api_call_error_handler(sipVirtErrorHandlerFunc error_handler,
        PyObject *w_inst, sip_gilstate_t gil_state);
PyObject *sip_api_call_method(PyObject *mod, int *is_err_p, PyObject *method,
        const char *fmt, ...);
void sip_api_call_procedure_method(PyObject *mod, sip_gilstate_t gil_state,
        sipVirtErrorHandlerFunc error_handler, PyObject *py_self,
        PyObject *method, const char *fmt, ...);
int sip_api_can_convert_to_type(PyObject *mod, PyObject *pyObj,
        sipTypeID type_id, int flags);
PyObject *sip_api_convert_from_new_pytype(PyObject *mod, void *cpp,
        PyTypeObject *w_type, PyObject *owner, PyObject **self_p,
        const char *fmt, ...);
PyObject *sip_api_convert_from_new_type(PyObject *mod, void *cpp,
        sipTypeID type_id, PyObject *transferObj);
PyObject *sip_api_convert_from_type(PyObject *mod, void *cpp,
        sipTypeID type_id, PyObject *transferObj);
void *sip_api_convert_to_type(PyObject *mod, PyObject *pyObj,
        sipTypeID type_id, PyObject *transferObj, int flags, int *statep,
        int *iserrp);
void *sip_api_convert_to_type_us(PyObject *mod, PyObject *pyObj,
        sipTypeID type_id, PyObject *transferObj, int flags, int *statep,
        void **user_statep, int *iserrp);
void *sip_api_force_convert_to_type(PyObject *mod, PyObject *pyObj,
        sipTypeID type_id, PyObject *transferObj, int flags, int *statep,
        int *iserrp);
void *sip_api_force_convert_to_type_us(PyObject *mod, PyObject *pyObj,
        sipTypeID type_id, PyObject *transferObj, int flags, int *statep,
        void **user_statep, int *iserrp);
PyObject *sip_api_get_pyobject(PyObject *mod, void *cppPtr, sipTypeID type_id);
bool sip_api_parse_args(PyObject *mod, const char *type_hint,
        PyObject **p_state_p, PyObject *args, const char *fmt, ...);
bool sip_api_parse_kwd_args(PyObject *mod, const char *type_hint,
        PyObject **p_state_p, PyObject *args, PyObject *kwargs,
        const char **kwd_list, const char *fmt, ...);
bool sip_api_parse_pair(PyObject *mod, const char *type_hint,
        PyObject **p_state_p, PyObject *arg_0, PyObject *arg_1,
        const char *fmt, ...);
bool sip_api_parse_vc_kwd_args(PyObject *mod, const char *type_hint,
        PyObject **p_state_p, PyObject *const *args, Py_ssize_t nr_pos_args,
        PyObject *kwd_names, const char **kwd_list, PyObject **unused_p,
        const char *fmt, ...);
int sip_api_parse_result(PyObject *mod, sip_gilstate_t gil_state,
        sipVirtErrorHandlerFunc error_handler, PyObject *w_inst,
        PyObject *method, PyObject *res, const char *fmt, ...);
void sip_api_release_type(PyObject *mod, void *cpp, sipTypeID type_id,
        int state);
void sip_api_release_type_us(PyObject *mod, void *cpp, sipTypeID type_id,
        int state, void *user_state);

PyObject *sip_convert_from_type(sipModuleState *ms, void *cppPtr,
        sipTypeID type_id, PyObject *transferObj);
void *sip_force_convert_to_type_us(sipModuleState *ms, PyObject *pyObj,
        sipTypeID type_id, PyObject *transferObj, int flags, int *statep,
        void **user_statep, int *iserrp);
PyObject *sip_is_py_method(sipModuleState *ms, sip_gilstate_t *gil,
        char *pymc, PyObject **self_p, const char *cname, const char *mname);
void sip_no_callable(PyObject *p_state, const char *scope, const char *method);
void sip_release(void *addr, const sipTypeSpec *td, int state,
        void *user_state);
int sip_vectorcall_create(PyObject *args, PyObject *kwargs,
        PyObject **small_argv, Py_ssize_t *argv_len_p, PyObject ***argv_p,
        Py_ssize_t *nr_pos_args_p, PyObject **kw_names_p);
void sip_vectorcall_dispose(PyObject **small_argv, PyObject **argv,
        Py_ssize_t argv_len, PyObject *kw_names);

#ifdef __cplusplus
}
#endif

#endif
