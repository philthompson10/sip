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
        PyObject *name, const sipUnboundAttributesDef *uad);
int sip_mod_con_setattro(sipWrappedModuleState *wms, PyObject *self,
        PyObject *name, PyObject *value, const sipUnboundAttributesDef *uad);
int sip_module_wrapper_init(PyObject *module, sipSipModuleState *sms);


#ifdef __cplusplus
}
#endif

#endif
