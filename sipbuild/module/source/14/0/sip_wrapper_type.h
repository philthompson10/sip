/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the sip wrapper type type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_WRAPPER_TYPE_H
#define _SIP_WRAPPER_TYPE_H


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip.h"


#ifdef __cplusplus
extern "C" {
#endif


struct _sipSipModuleState;


/*
 * The meta-type of a wrapper type.
 */
struct _sipWrapperType {
    /*
     * The super-metatype.  This must be first in the structure so that it can
     * be cast to a PyTypeObject *.
     */
    PyHeapTypeObject super;

    /* Set if the type is a user implemented Python sub-class. */
    unsigned wt_user_type : 1;

    /* Set if the type's dictionary contains all lazy attributes. */
    unsigned wt_dict_complete : 1;

    /* Unused and available for future use. */
    unsigned wt_unused : 30;

    /* The generated type structure. */
    const sipTypeDef *wt_td;

    /* The list of init extenders. */
    struct _sipInitExtenderDef *wt_iextend;

    /*
     * For the user to use.  Note that any data structure will leak if the
     * type is garbage collected.
     */
    // TODO Should this be a PyObject?
    void *wt_user_data;
};


int sip_wrapper_type_init(PyObject *module, struct _sipSipModuleState *sms);


#ifdef __cplusplus
}
#endif

#endif
