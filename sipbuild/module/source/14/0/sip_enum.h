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
#if defined(SIP_CONFIGURATION_PyEnums)
int sip_api_is_enum_flag(PyObject *wmod, PyObject *obj);
#endif

/* These are internal. */
#if defined(SIP_CONFIGURATION_PyEnums)
#if 0
PyTypeObject *sip_enum_create_py_enum(sipWrappedModuleState *wms,
        const sipEnumTypeDef *etd, const sipIntInstanceDef **next_int_p,
        PyObject *dict);
#endif
#endif

#if defined(SIP_CONFIGURATION_CustomEnums)
/*
 * The meta-type of a custom enum type.
 */
typedef struct {
    /*
     * The super-metatype.  This must be first in the structure so that it can
     * be cast to a PyTypeObject *.
     */
    PyHeapTypeObject super;

    /* The generated type structure. */
    const sipTypeDef *type;
} sipEnumTypeObject;


PyTypeObject *sip_enum_create_custom_enum(sipSipModuleState *sms,
        const sipWrappedModuleDef *wmd, const sipEnumTypeDef *etd, int enum_nr,
        PyObject *wmod_dict);
PyObject *sip_enum_pickle_custom_enum(PyObject *self,
        PyTypeObject *defining_class, PyObject *const *args, Py_ssize_t nargs,
        PyObject *kwd_args);
PyObject *sip_enum_unpickle_custom_enum(PyObject *mod, PyObject *args);
#endif

PyObject *sip_enum_convert_from_enum(sipWrappedModuleState *wms, int member,
        sipTypeID type_id);
int sip_enum_convert_to_constrained_enum(sipWrappedModuleState *wms,
        PyObject *obj, sipTypeID type_id);
int sip_enum_convert_to_enum(sipWrappedModuleState *wms, PyObject *obj,
        sipTypeID type_id);
int sip_enum_init(PyObject *module, sipSipModuleState *sms);
int sip_enum_is_enum(sipSipModuleState *sms, PyObject *obj);


#ifdef __cplusplus
}
#endif

#endif
