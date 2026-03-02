/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the sip wrapper type type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_WRAPPER_TYPE_H
#define _SIP_WRAPPER_TYPE_H

#include <Python.h>

#include "sip.h"
#include "sip_core.h"
#include "sip_decls.h"


#ifdef __cplusplus
extern "C" {
#endif

/*
 * The meta-type of a wrapper type.
 */
struct _sipWrapperType {
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
    unsigned wt_user_type : 1;

    /* Unused and available for future use. */
    unsigned wt_unused : 29;

    /* A strong reference to the defining module. */
    PyObject *wt_d_mod;

    /* The type ID in the context of the defining module. */
    // TODO Why not just a type nr?
    sipTypeID wt_type_id;

    /* The list of init extenders. */
    struct _sipInitExtenderDef *wt_iextend;

    /*
     * For the user to use.  This is a strong reference.
     */
    PyObject *wt_user_data;
};


int sip_wrapper_type_init(PyObject *module, sipSipModuleState *sms);


/*
 * Return the type specification for a wrapper type.
 */
static inline const sipTypeSpec *sip_get_type_spec_from_wt(sipWrapperType *wt)
{
    return sip_get_type_spec((sipModuleState *)PyModule_GetState(wt->wt_d_mod),
            wt->wt_type_id);
}

#ifdef __cplusplus
}
#endif

#endif
