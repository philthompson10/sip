/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The sip module interface.
 *
 * Copyright (c) 2026 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_MODULE_H
#define _SIP_MODULE_H

#include <Python.h>

#include "sip.h"
#include "sip_decls.h"
#include "sip_object_map.h"
#include "sip_wrapped_module.h"


#ifdef __cplusplus
extern "C" {
#endif

/* The sip module's state. */
struct _sipSipModuleState {
    /* The sip.array type object. */
    PyTypeObject *array_type;

    /* The sip.callable type object. */
    PyTypeObject *callable_type;

#if defined(SIP_CONFIGURATION_CustomEnums)
    /* The sip.enumtype type object. */
    PyTypeObject *custom_enum_type;
#endif

#if defined(SIP_CONFIGURATION_PyEnums)
    /* The builtin int type object. */
    PyObject *builtin_int_type;

    /* The builtin object type object. */
    PyObject *builtin_object_type;
#endif

    /* The empty tuple. */
    PyObject *empty_tuple;

    /* The enum.Enum type object. */
    PyObject *enum_enum_type;

    /* The enum.IntEnum type object. */
    PyObject *enum_int_enum_type;

#if defined(SIP_CONFIGURATION_PyEnums)
    /* The enum.Flag type object. */
    PyObject *enum_flag_type;

    /* The enum.IntFlag type object. */
    PyObject *enum_int_flag_type;
#endif

    /* The event handler lists. */
    sipEventHandler *event_handlers[sipEventNrEvents];

    /* The interpreter state. */
    PyInterpreterState *interpreter_state;

    /* The method descriptor type object. */
    PyTypeObject *method_descr_type;

    /* The list of registered modules. */
    PyObject *module_list;

    /* The sip.modulewrapper type object. */
    PyTypeObject *module_wrapper_type;

    /* The object map. */
    sipObjectMap object_map;

    /* The list of registered Python type objects. */
    PyObject *registered_py_types;

    /* The sip.simplewrapper type object. */
    PyTypeObject *simple_wrapper_type;

    /* The list of symbols. */
    sipSymbol *symbol_list;

    /* The list of threads. */
    sipThread *thread_list;

    /* The trace mask. */
    unsigned trace_mask;

    /* The variable descriptor type object. */
    PyTypeObject *variable_descr_type;

    /* The sip.voidptr type object. */
    PyTypeObject *void_ptr_type;

    /* The sip.wrapper type object. */
    PyTypeObject *wrapper_type;

    /* The sip.wrappertype type object. */
    PyTypeObject *wrapper_type_type;
};


PyObject *sip_get_sip_module(PyTypeObject *defining_class);
sipSipModuleState *sip_get_sip_module_state_from_type(PyTypeObject *type);
int sip_sip_module_clear(sipSipModuleState *sms);
void sip_sip_module_free(sipSipModuleState *sms);
int sip_sip_module_init(sipSipModuleState *sms, PyObject *mod);
int sip_sip_module_traverse(sipSipModuleState *sms, visitproc visit,
        void *arg);


/*
 * Return the sip module's state.
 */
static inline sipSipModuleState *sip_get_sip_module_state(PyObject *smod)
{
#if _SIP_MODULE_SHARED
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(smod);

    assert(sms != NULL);

    return sms;
#else
    /* The module is actually the wrapped module. */
    sipModuleState *ms = sip_get_module_state(smod);

    return ms->sip_module_state;
#endif
}

#ifdef __cplusplus
}
#endif

#endif
