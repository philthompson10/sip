/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the sip wrapper type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_WRAPPER_H
#define _SIP_WRAPPER_H


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip.h"

#include "sip_simple_wrapper.h"


#ifdef __cplusplus
extern "C" {
#endif


/*
 * The type of a C/C++ wrapper object that supports parent/child relationships.
 * A parent holds a strong reference to each of its children.
 */
struct _sipWrapper {
    /* The super-type. */
    sipSimpleWrapper super;

    /* First child object. */
    PyObject *first_child;

    /* Next sibling. */
    PyObject *sibling_next;

    /* Previous sibling. */
    PyObject *sibling_prev;

    /* Owning object. */
    PyObject *parent;
};


int sip_wrapper_init(PyObject *module, sipSipModuleState *sms);


#ifdef __cplusplus
}
#endif

#endif
