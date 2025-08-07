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

#include "sip_simple_wrapper.h"


#ifdef __cplusplus
extern "C" {
#endif


struct _sipSipModuleState;


/*
 * The type of a C/C++ wrapper object that supports parent/child relationships.
 * A parent holds a strong reference to each of its children.
 */
struct _sipWrapper {
    /* The super-type. */
    struct _sipSimpleWrapper super;

    /* First child object. */
    struct _sipWrapper *first_child;

    /* Next sibling. */
    struct _sipWrapper *sibling_next;

    /* Previous sibling. */
    struct _sipWrapper *sibling_prev;

    /* Owning object. */
    struct _sipWrapper *parent;
};


int sip_wrapper_init(PyObject *module, struct _sipSipModuleState *sms);


#ifdef __cplusplus
}
#endif

#endif
