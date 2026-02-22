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

/*
 * An imported wrapped module.  Note that we don't refer to it as a state to
 * avoid confusion with the Python module state.
 */
typedef struct _sipImportedModule {
    /* A strong reference to the module. */
    PyObject *module;

    /*
     * An array on the heap mapping contextual type numbers to defining type
     * numbers.
     */
    sipTypeNr *type_nr_map;
} sipImportedModule;


/*
 * An extender for a class that hasn't yet been created.
 */
typedef struct _sipPendingExtender {
    /* The specification of the class that is to be extended. */
    const sipClassTypeSpec *extending;

    /* A weak reference to the module containing the extender. */
    PyObject *extender_module;

    /* The type ID of the pending extender. */
    sipTypeID extender_id;

    /* The next in the linked list of pending extenders. */
    struct _sipPendingExtender *next;
} sipPendingExtender;


/*
 * A wrapped module's state.
 */
struct _sipModuleState {
    /* The list of delayed dtors. */
    sipDelayedDtor *delayed_dtors_list;

    /* The optional dictionary of extra references using an int key. */
    PyObject *extra_refs;

    /* The array of imported modules. */
    sipImportedModule *imported_modules;

    /*
     * The array of type object references accessed using the type ID.  These
     * can be wrapper types, custom enum types or Python enum types.  We don't
     * use a Python list because some elements can be NULL (ie. related to a
     * mapped type or an external class).
     */
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

    /* The specification of the wrapped module. */
    const sipModuleSpec *module_spec;

    /* The linked list of any pending class extenders. */
    sipPendingExtender *pending_extenders;

    // TODO Extensions to the state provided by the bindings author.  It must
    // be a PyObject (or another type that can be garbage collected) and may
    // need to support multiple states from different bindings.
    // PyObject *user;
};


int sip_api_module_clear(PyObject *mod);
void sip_api_module_free(void *mod_ptr);
int sip_api_module_traverse(PyObject *mod, visitproc visit, void *arg);


/*
 * Return a wrapped module's state.
 */
static inline sipModuleState *sip_get_module_state(PyObject *mod)
{
    sipModuleState *ms = (sipModuleState *)PyModule_GetState(mod);

    assert(ms != NULL);

    return ms;
}

#ifdef __cplusplus
}
#endif

#endif
