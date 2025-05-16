/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the enum support.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_ENUM_H
#define _SIP_ENUM_H


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip.h"


#ifdef __cplusplus
extern "C" {
#endif

/* These are part of the module API. */
PyObject *sip_api_convert_from_enum(int member, const sipTypeDef *td);
int sip_api_convert_to_enum(PyObject *obj, const sipTypeDef *td);
#if defined(SIP_CONFIGURATION_PyEnums)
int sip_api_is_enum_flag(PyObject *obj);
#endif

/* These are internal. */
#if defined(SIP_CONFIGURATION_PyEnums)
int sip_enum_create_py_enum(sipExportedModuleDef *client, sipEnumTypeDef *etd,
        sipIntInstanceDef **next_int_p, PyObject *dict);
#endif

#if defined(SIP_CONFIGURATION_CustomEnums)
/*
 * The meta-type of a custom enum type.
 */
struct _sipEnumTypeObject {
    /*
     * The super-metatype.  This must be first in the structure so that it can
     * be cast to a PyTypeObject *.
     */
    PyHeapTypeObject super;

    /* The generated type structure. */
    sipTypeDef *type;
};

typedef struct _sipEnumTypeObject sipEnumTypeObject;

extern PyObject *sip_enum_custom_enum_unpickler;

int sip_enum_create_custom_enum(sipExportedModuleDef *client,
        sipEnumTypeDef *etd, int enum_nr, PyObject *mod_dict);
PyObject *sip_enum_pickle_custom_enum(PyObject *obj, PyObject *args);
PyObject *sip_enum_unpickle_custom_enum(PyObject *obj, PyObject *args);
#endif

int sip_enum_convert_to_constrained_enum(PyObject *obj, const sipTypeDef *td);
const sipTypeDef *sip_enum_get_generated_type(PyTypeObject *py_type);
int sip_enum_init(void);
int sip_enum_is_enum(PyObject *obj);


#ifdef __cplusplus
}
#endif

#endif
