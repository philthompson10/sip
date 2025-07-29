/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The sip module interface.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_MODULE_H
#define _SIP_MODULE_H


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip.h"
#include "sip_core.h"
#include "sip_object_map.h"
#include "sip_threads.h"


#ifdef __cplusplus
extern "C" {
#endif


/* The sip module's state. */
typedef struct _sipSipModuleState {
    /* The sip.array type object. */
    PyTypeObject *array_type;

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

#if defined(SIP_CONFIGURATION_CustomEnums)
    /* The type definition used in creating the current custom enum. */
    // TODO Try and get rid of this.
    const sipTypeDef *current_enum_backdoor;
#endif

    /* The type definition used in creating the current type. */
    // TODO Try and get rid of this.
    const sipTypeDef *current_type_def_backdoor;

    /* The empty tuple. */
    PyObject *empty_tuple;

#if defined(SIP_CONFIGURATION_CustomEnums)
    /* The enum.Enum type object. */
    PyObject *enum_enum_type;

    /* The enum.IntEnum type object. */
    PyObject *enum_int_enum_type;
#endif

#if defined(SIP_CONFIGURATION_PyEnums)
    /* The enum.Enum type object. */
    PyObject *enum_enum_type;

    /* The enum.IntEnum type object. */
    PyObject *enum_int_enum_type;

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

    /* For passing a dict of unused arguments. */
    // TODO Try and get rid of this.
    PyObject **unused_backdoor;

    /* The variable descriptor type object. */
    PyTypeObject *variable_descr_type;

    /* The sip.voidptr type object. */
    PyTypeObject *void_ptr_type;

    /* The sip.wrapper type object. */
    PyTypeObject *wrapper_type;

    /* The sip.wrappertype type object. */
    PyTypeObject *wrapper_type_type;
} sipSipModuleState;


// TODO Review if the first 3 are needed.
PyObject *sip_get_sip_module(PyTypeObject *defining_class);
sipSipModuleState *sip_get_sip_module_state_from_any_type(PyTypeObject *type);
sipSipModuleState *sip_get_sip_module_state_from_wrapper_type(
        PyTypeObject *wt);
int sip_sip_module_clear(sipSipModuleState *sms);
void sip_sip_module_free(sipSipModuleState *sms);
int sip_sip_module_init(sipSipModuleState *sms, PyObject *mod);
int sip_sip_module_traverse(sipSipModuleState *sms, visitproc visit,
        void *arg);


#ifdef __cplusplus
}
#endif

#endif
