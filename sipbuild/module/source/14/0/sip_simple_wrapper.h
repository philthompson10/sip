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

#include "sip.h"


#ifdef __cplusplus
extern "C" {
#endif


struct _sipSipModuleState;


/*
 * The type of a simple C/C++ wrapper object.
 */
struct _sipSimpleWrapper {
    PyObject_HEAD

    /* The type's immutable definition. */
    const sipClassTypeDef *ctd;

    /* The data, ie. a pointer to the C/C++ object. */
    void *data;

    /* The instance dictionary. */
    PyObject *dict;

    /* A strong reference to the defining module. */
    PyObject *dmod;

    /* The optional dictionary of extra references using an int key. */
    PyObject *extra_refs;

    /* Object flags. */
    unsigned flags;

    /* The main instance if this is a mixin. */
    PyObject *mixin_main;

    /* Next object at this address. */
    struct _sipSimpleWrapper *next;

    /* For the user to use. */
    PyObject *user;
};


int sipSimpleWrapper_clear(PyObject *self);
int sipSimpleWrapper_traverse(PyObject *self, visitproc visit, void *arg);
//int sipSimpleWrapper_getbuffer(PyObject *self, Py_buffer *buf, int flags);
//void sipSimpleWrapper_releasebuffer(PyObject *self, Py_buffer *buf);

void sip_api_simple_wrapper_configure(sipSimpleWrapper *self, PyObject *dmod,
        const sipClassTypeDef *ctd);
int sip_api_simple_wrapper_init(sipSimpleWrapper *self, PyObject *args,
        PyObject *kwd_args);

int sip_simple_wrapper_init(PyObject *module, struct _sipSipModuleState *sms);


#ifdef __cplusplus
}
#endif

#endif
