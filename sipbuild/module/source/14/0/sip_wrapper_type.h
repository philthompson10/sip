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


/*
 * The meta-type of a wrapper type.
 */
typedef struct {
    /*
     * The super-metatype.  This must be first in the structure so that it can
     * be cast to a PyTypeObject *.
     */
    PyHeapTypeObject super;

    /* Set if autoconversion of the type is disabled. */
    unsigned wt_autoconversion_disabled : 1;

    /*
     * Set if the type is a sub-type of wrapper rather than simple wrapper.
     * This can only be used if we know we have a SIP generated type.
     */
    unsigned wt_is_wrapper : 1;

    /* Set if the type is a user implemented Python sub-class. */
    // TODO Is this still needed?
    unsigned wt_user_type : 1;

    /* Unused and available for future use. */
    unsigned wt_unused : 29;

    /* A strong reference to the defining module. */
    PyObject *wt_dmod;

    /* The wrapped type definition. */
    const sipTypeDef *wt_td;

    /* The generated absolute type ID. */
    // TODO This gives access to the type definition but also allows access to
    // this type object later on without having to explicitly pass this type
    // object.  Note that it isn't set up yet.  It is useful in Array_new() but
    // is it useful elsewhere?  The problem is that sip_convert_from_type()
    // needs the type object because it needs to check the autoconversion flag.
    // If we get rid of it then we might also get rid of the idea of absolute
    // type IDs.
    sipTypeID wt_type_id;

    /* The list of init extenders. */
    struct _sipInitExtenderDef *wt_iextend;

    /*
     * For the user to use.  Note that any data structure will leak if the
     * type is garbage collected.
     */
    // TODO Should this be a PyObject?
    void *wt_user_data;
} sipWrapperType;


int sip_wrapper_type_init(PyObject *module, sipSipModuleState *sms);


#ifdef __cplusplus
}
#endif

#endif
