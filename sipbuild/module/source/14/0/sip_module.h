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


#ifdef __cplusplus
extern "C" {
#endif


/* The sip module's state. */
typedef struct {
    // The sip.array type object.
    PyTypeObject *array_type;

    // The method descriptor type object.
    PyTypeObject *method_descr_type;

    // The variable descriptor type object.
    PyTypeObject *variable_descr_type;

    // The void pointer type object.
    PyTypeObject *void_ptr_type;
} sipSipModuleState;


sipSipModuleState *sip_get_sip_module_state(PyObject *wmod);
sipSipModuleState *sip_get_sip_module_state_from_type(PyTypeObject *type);


#ifdef __cplusplus
}
#endif

#endif
