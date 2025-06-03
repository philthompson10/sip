/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the voidptr type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_VOIDPTR_H
#define _SIP_VOIDPTR_H


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip_module.h"


#ifdef __cplusplus
extern "C" {
#endif


void *sip_api_convert_to_void_ptr(PyObject *obj);
PyObject *sip_api_convert_from_void_ptr(void *val);
PyObject *sip_api_convert_from_const_void_ptr(const void *val);
PyObject *sip_api_convert_from_void_ptr_and_size(void *val, Py_ssize_t size);
PyObject *sip_api_convert_from_const_void_ptr_and_size(const void *val,
        Py_ssize_t size);
int sip_void_ptr_init(PyObject *module, sipSipModuleState *sms);


#ifdef __cplusplus
}
#endif

#endif
