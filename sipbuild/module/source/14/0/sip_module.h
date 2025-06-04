/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The sip module interface.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_MODULE_H
#define _SIP_MODULE_H


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip.h"


#ifdef __cplusplus
extern "C" {
#endif


/* The sip module's state. */
typedef struct {
    // The sip.array type object.
    PyTypeObject *array_type;

#if defined(SIP_CONFIGURATION_CustomEnums)
    // The sip.enumtype type object.
    PyTypeObject *custom_enum_type;
#endif

    // The method descriptor type object.
    PyTypeObject *method_descr_type;

    // The sip.simplewrapper type object.
    PyTypeObject *simple_wrapper_type;

    // The variable descriptor type object.
    PyTypeObject *variable_descr_type;

    // The sip.voidptr type object.
    PyTypeObject *void_ptr_type;

    // The sip.wrapper type object.
    PyTypeObject *wrapper_type;

    // The sip.wrappertype type object.
    PyTypeObject *wrapper_type_type;
} sipSipModuleState;


sipSipModuleState *sip_get_sip_module_state(PyObject *wmod);
sipSipModuleState *sip_get_sip_module_state_from_any_type(PyTypeObject *type);


#ifdef __cplusplus
}
#endif

#endif
