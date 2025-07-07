/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the method descriptor type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_METHOD_DESCRIPTOR_H
#define _SIP_METHOD_DESCRIPTOR_H


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip.h"


#ifdef __cplusplus
extern "C" {
#endif


PyObject *sipMethodDescr_New(sipSipModuleState *sms, const PyMethodDef *pmd);
PyObject *sipMethodDescr_Copy(sipSipModuleState *sms, PyObject *orig,
        PyObject *mixin_name);
int sip_method_descr_init(PyObject *module, sipSipModuleState *sms);


#ifdef __cplusplus
}
#endif

#endif
