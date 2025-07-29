/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The sip module implementation.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <assert.h>

#include "sip.h"

#include "sip_array.h"
#include "sip_core.h"
#include "sip_enum.h"
#include "sip_method_descriptor.h"
#include "sip_module.h"
#include "sip_module_wrapper.h"
#include "sip_object_map.h"
#include "sip_variable_descriptor.h"
#include "sip_voidptr.h"
#include "sip_wrapped_module.h"
#include "sip_wrapper.h"
#include "sip_wrapper_type.h"


#if _SIP_MODULE_SHARED

/* Forward declarations specific to a standalone sip module. */
static sipWrappedModuleInitFunc bootstrap(int abi_major);
static int module_clear(PyObject *module);
static int module_exec(PyObject *module);
static void module_free(void *module_ptr);
static int module_traverse(PyObject *module, visitproc visit, void *arg);


/* The standalone sip module definition. */
static PyModuleDef_Slot module_slots[] = {
    {Py_mod_exec, module_exec},
#if PY_VERSION_HEX >= 0x030c0000
    {Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED},
#endif
#if PY_VERSION_HEX >= 0x030d0000
    {Py_mod_gil, Py_MOD_GIL_USED},
#endif
    {0, NULL},
};

static PyModuleDef module_def = {
    .m_base = PyModuleDef_HEAD_INIT,
    .m_name = _SIP_MODULE_FQ_NAME,
    .m_doc = PyDoc_STR("Bindings related utilities"),
    .m_size = sizeof (sipSipModuleState),
    .m_slots = module_slots,
    .m_clear = module_clear,
    .m_traverse = module_traverse,
    .m_free = module_free,
};


/*
 * The standalone sip module initialisation function.
 */
#if defined(SIP_STATIC_MODULE)
PyObject *_SIP_MODULE_ENTRY(void)
#else
PyMODINIT_FUNC _SIP_MODULE_ENTRY(void)
#endif
{
    return PyModuleDef_Init(&module_def);
}


/*
 * Implement the exec phase of the module initialisation.
 */
static int module_exec(PyObject *module)
{
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(module);

    /* Initialise the module. */
    if (sip_sip_module_init(sms, module) < 0)
        return -1;

    /* Publish the bootstrap function. */
    PyObject *api_obj = PyCapsule_New((void *)bootstrap, "_C_BOOTSTRAP", NULL);

    int rc = PyModule_AddObjectRef(module, "_C_BOOTSTRAP", api_obj);
    Py_XDECREF(api_obj);

    return rc;
}


/*
 * Implement the standalone module clear slot.
 */
static int module_clear(PyObject *module)
{
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(module);

    return sip_sip_module_clear(sms);
}


/*
 * Implement the standalone module free slot.
 */
static void module_free(void *module_ptr)
{
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(
            (PyObject *)module_ptr);

    sip_sip_module_free(sms);
}


/*
 * Implement the standalone module traverse slot.
 */
static int module_traverse(PyObject *module, visitproc visit, void *arg)
{
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(module);

    return sip_sip_module_traverse(sms, visit, arg);
}


/*
 * The bootstrap function.
 */
static sipWrappedModuleInitFunc bootstrap(int abi_major)
{
    // TODO Verify abi_major.
    return sip_api_wrapped_module_init;
}
#endif


/*
 * Implement the module initialisation support.
 */
int sip_sip_module_init(sipSipModuleState *sms, PyObject *mod)
{
    sms->current_type_def_backdoor = NULL;
    sms->module_list = NULL;
    sms->registered_py_types = NULL;
    sms->symbol_list = NULL;
    sms->thread_list = NULL;
    sms->unused_backdoor = NULL;

    /* Initialise the types. */
    if (sip_wrapper_type_init(mod, sms) < 0 ||
        sip_simple_wrapper_init(mod, sms) < 0 ||
        sip_wrapper_init(mod, sms) < 0 ||
        sip_module_wrapper_init(mod, sms) < 0 ||
        sip_method_descr_init(mod, sms) < 0 ||
        sip_variable_descr_init(mod, sms) < 0 ||
        sip_enum_init(mod, sms) < 0 ||
        sip_void_ptr_init(mod, sms) < 0 ||
        sip_array_init(mod, sms) < 0)
        return -1;

    if (sip_register_py_type(sms, sms->simple_wrapper_type) < 0)
        return -1;

    /* This will always be needed. */
#if PY_VERSION_HEX >= 0x030d0000
    sms->empty_tuple = Py_GetConstant(Py_CONSTANT_EMPTY_TUPLE);
#else
    if ((sms->empty_tuple = PyTuple_New(0)) == NULL)
        return -1;
#endif

    /* Initialise the object map. */
    sip_om_init(&sms->object_map);

    /*
     * Get the current interpreter state.  This will be shared between all
     * threads.
     */
    sms->interpreter_state = PyThreadState_Get()->interp;

    return 0;
}


/*
 * Implement the module clear support.
 */
int sip_sip_module_clear(sipSipModuleState *sms)
{
    Py_CLEAR(sms->array_type);
#if defined(SIP_CONFIGURATION_PyEnums)
    Py_CLEAR(sms->builtin_int_type);
    Py_CLEAR(sms->builtin_object_type);
#endif
#if defined(SIP_CONFIGURATION_CustomEnums)
    Py_CLEAR(sms->custom_enum_type);
#endif
    Py_CLEAR(sms->empty_tuple);
#if defined(SIP_CONFIGURATION_CustomEnums)
    Py_CLEAR(sms->enum_enum_type);
    Py_CLEAR(sms->enum_int_enum_type);
#endif
#if defined(SIP_CONFIGURATION_PyEnums)
    Py_CLEAR(sms->enum_enum_type);
    Py_CLEAR(sms->enum_int_enum_type);
    Py_CLEAR(sms->enum_flag_type);
    Py_CLEAR(sms->enum_int_flag_type);
#endif
    Py_CLEAR(sms->method_descr_type);
    Py_CLEAR(sms->module_wrapper_type);
    Py_CLEAR(sms->simple_wrapper_type);
    Py_CLEAR(sms->variable_descr_type);
    Py_CLEAR(sms->void_ptr_type);
    Py_CLEAR(sms->wrapper_type);
    Py_CLEAR(sms->wrapper_type_type);

    Py_CLEAR(sms->module_list);
    Py_CLEAR(sms->registered_py_types);

    return 0;
}


/*
 * Implement the module free support.
 */
void sip_sip_module_free(sipSipModuleState *sms)
{
    /* Shutdown all virtual reimplementations. */
    // TODO This should probably be done at the start of the shoutdown process
    // rather than here, ie. before any remaining C++ code (eg. delayed dtors)
    // gets executed.  We could make the virtuals dependent on a flag in the
    // wrapped module state instead.
    sms->interpreter_state = NULL;

    sip_sip_module_clear(sms);

    /* Free the event handlers. */
    int i;

    for (i = 0; i < sipEventNrEvents; ++i)
    {
        sipEventHandler *eh = sms->event_handlers[i];

        while (eh != NULL)
        {
            sipEventHandler *next = eh->next;

            sip_api_free(eh);
            eh = next;
        }
    }

    /* Free the symbols. */
    sipSymbol *ss = sms->symbol_list;;

    while (ss != NULL)
    {
        sipSymbol *next = ss->next;

        sip_api_free(ss);
        ss = next;
    }

    /* Free the threads. */
    sipThread *thread = sms->thread_list;

    while (thread != NULL)
    {
        sipThread *next = thread->next;

        sip_api_free(thread);
        thread = next;
    }

    /* Free the object map. */
    sip_om_finalise(&sms->object_map);
}


/*
 * Implement the module traverse support.
 */
int sip_sip_module_traverse(sipSipModuleState *sms, visitproc visit, void *arg)
{
    Py_VISIT(sms->array_type);
#if defined(SIP_CONFIGURATION_PyEnums)
    Py_VISIT(sms->builtin_int_type);
    Py_VISIT(sms->builtin_object_type);
#endif
#if defined(SIP_CONFIGURATION_CustomEnums)
    Py_VISIT(sms->custom_enum_type);
#endif
    Py_VISIT(sms->empty_tuple);
#if defined(SIP_CONFIGURATION_CustomEnums)
    Py_VISIT(sms->enum_enum_type);
    Py_VISIT(sms->enum_int_enum_type);
#endif
#if defined(SIP_CONFIGURATION_PyEnums)
    Py_VISIT(sms->enum_enum_type);
    Py_VISIT(sms->enum_int_enum_type);
    Py_VISIT(sms->enum_flag_type);
    Py_VISIT(sms->enum_int_flag_type);
#endif
    Py_VISIT(sms->method_descr_type);
    Py_VISIT(sms->module_wrapper_type);
    Py_VISIT(sms->simple_wrapper_type);
    Py_VISIT(sms->variable_descr_type);
    Py_VISIT(sms->void_ptr_type);
    Py_VISIT(sms->wrapper_type);
    Py_VISIT(sms->wrapper_type_type);

    Py_VISIT(sms->module_list);
    Py_VISIT(sms->registered_py_types);

    return 0;
}


/*
 * Return the sip module from a defining (ie. wrapped) class.
 */
// TODO Review the need for this.
PyObject *sip_get_sip_module(PyTypeObject *defining_class)
{
    return ((sipWrappedModuleState *)PyType_GetModuleState(defining_class))->sip_module;
}


/*
 * Return the state for the sip module from any type.  NULL is returned if the
 * type wasn't created by the sip module.
 */
// TODO Review the need for this.
sipSipModuleState *sip_get_sip_module_state_from_any_type(PyTypeObject *type)
{
#if 0
    // TODO module_def isn't available with an embedded sip module.
    PyObject *mod = PyType_GetModuleByDef(type, &module_def);

    if (mod == NULL)
    {
        PyErr_Clear();
        return NULL;
    }

    return (sipSipModuleState *)PyModule_GetState(mod);
#else
    return NULL;
#endif
}


/*
 * Return the state for the sip module from a wrapper type (or a type known to
 * be associated with the sip module).
 */
// TODO Review the need for this.
sipSipModuleState *sip_get_sip_module_state_from_wrapper_type(PyTypeObject *wt)
{
#if 0
    // TODO module_def isn't available with an embedded sip module.
    PyObject *mod = PyType_GetModuleByDef(wt, &module_def);
    assert(mod != NULL);

    return (sipSipModuleState *)PyModule_GetState(mod);
#else
    return NULL;
#endif
}
