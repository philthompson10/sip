/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the variable descriptor type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_VARIABLE_DESCRIPTOR_H
#define _SIP_VARIABLE_DESCRIPTOR_H

#include <Python.h>

#include "sip.h"
#include "sip_decls.h"


#ifdef __cplusplus
extern "C" {
#endif

PyObject *sipVariableDescr_New(sipSipModuleState *sms, PyTypeObject *w_type,
        const sipAttrSpec *attr_spec);
PyObject *sipVariableDescr_Copy(sipSipModuleState *sms, PyObject *orig,
        PyObject *mixin_name);
int sip_variable_descr_init(PyObject *module, sipSipModuleState *sms);

#ifdef __cplusplus
}
#endif

#endif
