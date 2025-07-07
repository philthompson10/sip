/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the variable descriptor type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_VARIABLE_DESCRIPTOR_H
#define _SIP_VARIABLE_DESCRIPTOR_H


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip.h"


#ifdef __cplusplus
extern "C" {
#endif


PyObject *sipVariableDescr_New(sipSipModuleState *sms,
        const sipVariableDef *vd, const sipTypeDef *td, const char *cod_name);
PyObject *sipVariableDescr_Copy(sipSipModuleState *sms, PyObject *orig,
        PyObject *mixin_name);
int sip_variable_descr_init(PyObject *module, sipSipModuleState *sms);


#ifdef __cplusplus
}
#endif

#endif
