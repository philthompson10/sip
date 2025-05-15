/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the enum support using custom enums.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_CUSTOM_ENUM_H
#define _SIP_CUSTOM_ENUM_H


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip.h"


#if defined(SIP_CONFIGURE_CustomEnums)

#ifdef __cplusplus
extern "C" {
#endif


PyObject *sip_api_convert_from_enum(int member, const sipTypeDef *td);
int sip_api_convert_to_enum(PyObject *obj, const sipTypeDef *td);

//int sip_enum_create(sipExportedModuleDef *client, sipEnumTypeDef *etd,
//        sipIntInstanceDef **next_int_p, PyObject *dict);
int createEnum(sipExportedModuleDef *client, sipEnumTypeDef *etd, int type_index, PyObject *dict);
const sipTypeDef *sip_enum_get_generated_type(PyObject *obj);
// See sip_api_type_from_py_type_object() in v12 for the implementation of above
int sip_enum_is_enum(PyObject *obj);
// Above needed for '&', '^' format character support.  Lack of support in v12 is a bug.  Will need to import the enum module (see sip_enum_init() in v13.


#ifdef __cplusplus
}
#endif

#endif

#endif
