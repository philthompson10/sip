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


/* These are held in flags. */
#define SIP_PY_OWNED        0x0010  /* If owned by Python. */
#define SIP_SHARE_MAP       0x0020  /* If the map slot might be occupied. */
#define SIP_CPP_HAS_REF     0x0040  /* If C/C++ has a reference. */
#define SIP_POSSIBLE_PROXY  0x0080  /* If there might be a proxy slot. */
#define SIP_ALIAS           0x0100  /* If it is an alias. */
#define SIP_CREATED         0x0200  /* If the C/C++ object has been created. */

#define sipIsDerived(sw)    ((sw)->flags & SIP_DERIVED_CLASS)
#define sipIsPyOwned(sw)    ((sw)->flags & SIP_PY_OWNED)
#define sipSetPyOwned(sw)   ((sw)->flags |= SIP_PY_OWNED)
#define sipResetPyOwned(sw) ((sw)->flags &= ~SIP_PY_OWNED)
#define sipCppHasRef(sw)    ((sw)->flags & SIP_CPP_HAS_REF)
#define sipSetCppHasRef(sw) ((sw)->flags |= SIP_CPP_HAS_REF)
#define sipResetCppHasRef(sw)   ((sw)->flags &= ~SIP_CPP_HAS_REF)
#define sipPossibleProxy(sw)    ((sw)->flags & SIP_POSSIBLE_PROXY)
#define sipSetPossibleProxy(sw) ((sw)->flags |= SIP_POSSIBLE_PROXY)
#define sipIsAlias(sw)      ((sw)->flags & SIP_ALIAS)
#define sipWasCreated(sw)   ((sw)->flags & SIP_CREATED)


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


void sip_api_simple_wrapper_configure(sipSimpleWrapper *self, PyObject *dmod,
        const sipClassTypeDef *ctd);
int sip_api_simple_wrapper_init(sipSimpleWrapper *self, PyObject *args,
        PyObject *kwd_args);

int sip_simple_wrapper_init(PyObject *module, sipSipModuleState *sms);


#ifdef __cplusplus
}
#endif

#endif
