/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the sip simple wrapper type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_SIMPLE_WRAPPER_H
#define _SIP_SIMPLE_WRAPPER_H


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>


#ifdef __cplusplus
extern "C" {
#endif


/*
 * The type of a simple C/C++ wrapper object.
 */
struct _sipSimpleWrapper {
    PyObject_HEAD

    /*
     * The data, initially a pointer to the C/C++ object, as interpreted by the
     * access function.
     */
    void *data;

    /* The optional access function. */
    sipAccessFunc access_func;

    /* Object flags. */
    unsigned sw_flags;

    /* The optional dictionary of extra references keyed by argument number. */
    PyObject *extra_refs;

    /* For the user to use. */
    PyObject *user;

#if !defined(Py_TPFLAGS_MANAGED_DICT)
    /* The instance dictionary. */
    PyObject *dict;
#endif

    /* The main instance if this is a mixin. */
    PyObject *mixin_main;

    /* Next object at this address. */
    struct _sipSimpleWrapper *next;
};


extern PyType_Spec sipSimpleWrapper_TypeSpec;


#ifdef __cplusplus
}
#endif

#endif
