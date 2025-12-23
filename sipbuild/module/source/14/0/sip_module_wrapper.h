/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the sip module wrapper type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_MODULE_WRAPPER_H
#define _SIP_MODULE_WRAPPER_H

#include <Python.h>

#include "sip.h"
#include "sip_decls.h"


#ifdef __cplusplus
extern "C" {
#endif

PyObject *sip_mod_con_getattro(sipModuleState *wms, PyObject *self,
        PyObject *name, const sipAttrsSpec *wad);
int sip_mod_con_setattro(sipModuleState *wms, PyObject *self, PyObject *name,
        PyObject *value, const sipAttrsSpec *wad);
int sip_module_wrapper_init(PyObject *module, sipSipModuleState *sms);
PyObject *sip_variable_get(sipModuleState *wms, PyObject *instance,
        const sipVariableSpec *wvd, PyTypeObject *binding_type,
        PyObject *mixin_name);
int sip_variable_set(sipModuleState *wms, PyObject *instance, PyObject *value,
        const sipVariableSpec *wvd, PyTypeObject *binding_type,
        PyObject *mixin_name);

#ifdef __cplusplus
}
#endif

#endif
