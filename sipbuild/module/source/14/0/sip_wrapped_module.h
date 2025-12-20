/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the wrapped module support.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_WRAPPED_MODULE_H
#define _SIP_WRAPPED_MODULE_H

#include <Python.h>

#include "sip.h"
#include "sip_decls.h"


#ifdef __cplusplus
extern "C" {
#endif

/* A wrapped module's state. */
struct _sipWrappedModuleState {
    /*
     * The pointers to the functions that implement the sip module API.  This
     * *must* be the first field in order to keep this structure hidden.
     */
    const sipAPIDef *sip_api;

    /* The list of delayed dtors. */
    sipDelayedDtor *delayed_dtors_list;

    /* The optional dictionary of extra references using an int key. */
    PyObject *extra_refs;

    /* The list of imported modules. */
    PyObject *imported_modules;

    /*
     * The array of type object references accessed using the type ID.  These
     * can be wrapper types, custom enum types or Python enum types.  We don't
     * use a Python list because some elements can be NULL (ie. related to a
     * mapped type, a lazy attribute or an external class).
     */
    // TODO Use None rather than NULL and use a Python list?
    PyTypeObject **py_types;

    /* A strong reference to the sip module. */
    // TODO This is only actually used (via sip_get_sip_module()) to get the
    // two unpickle methods from its dict.  Can the unpickle methods be stored
    // in the sip module state?
    PyObject *sip_module;

    /* The module state of the sip module. */
    sipSipModuleState *sip_module_state;

    /* A borrowed reference to the wrapped module. */
    PyObject *wrapped_module;

    /* The definition of the wrapped module. */
    const sipWrappedModuleDef *wrapped_module_def;

    // TODO Extensions to the state provided by the bindings author.  It must
    // be a PyObject (or another type that can be garbage collected) and may
    // need to support multiple states from different bindings.
    // PyObject *user;
};


int sip_api_wrapped_module_clear(void *ms);
void sip_api_wrapped_module_free(void *ms);
int sip_api_wrapped_module_traverse(void *ms, visitproc visit, void *arg);

#ifdef __cplusplus
}
#endif

#endif
