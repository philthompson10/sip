/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the attribute support.
 *
 * Copyright (c) 2026 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_ATTRIBUTE_H
#define _SIP_ATTRIBUTE_H

#include <Python.h>

#include "sip.h"
#include "sip_decls.h"


#ifdef __cplusplus
extern "C" {
#endif

const sipAttrSpec *sip_get_attribute_spec(const char *name,
        const sipAttributesSpec *attrs);
PyObject *sip_mod_con_getattro(sipModuleState *ms, PyObject *self,
        PyObject *name, PyObject *attr_dict,
        const sipAttributesSpec *const attributes,
        const sipAttributesSpec *const static_variables,
        const sipTypeSpec *extending_ts);
int sip_mod_con_setattro(sipModuleState *ms, PyObject *self, PyObject *name,
        PyObject *value, const sipAttributesSpec *const attributes,
        const sipAttributesSpec *const static_variables,
        const sipTypeSpec *extending_ts);

#ifdef __cplusplus
}
#endif

#endif
