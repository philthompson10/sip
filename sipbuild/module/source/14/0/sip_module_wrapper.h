/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the sip module wrapper type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_MODULE_WRAPPER_H
#define _SIP_MODULE_WRAPPER_H


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip.h"


#ifdef __cplusplus
extern "C" {
#endif


PyObject *sip_mod_con_getattro(sipWrappedModuleState *wms, PyObject *self,
        PyObject *name, const sipWrappedAttrsDef *wad);
int sip_mod_con_setattro(sipWrappedModuleState *wms, PyObject *self,
        PyObject *name, PyObject *value, const sipWrappedAttrsDef *wad);
int sip_module_wrapper_init(PyObject *module, sipSipModuleState *sms);
PyObject *sip_variable_get(sipWrappedModuleState *wms, PyObject *instance,
        const sipWrappedVariableDef *wvd, sipWrapperType *binding_type,
        PyObject *mixin_name);
int sip_variable_set(sipWrappedModuleState *wms, PyObject *instance,
        PyObject *value, const sipWrappedVariableDef *wvd,
        sipWrapperType *binding_type, PyObject *mixin_name);


#ifdef __cplusplus
}
#endif

#endif
