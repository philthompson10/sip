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

#include "sip_module.h"
#include "sip_object_map.h"


/* Forward declarations. */
static int module_clear(PyObject *module);
static int module_exec(PyObject *module);
static void module_free(void *module_ptr);
static int module_traverse(PyObject *module, visitproc visit, void *arg);
static const sipAPIDef *bootstrap(int abi_major);


/* The module definition. */
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


#if _SIP_MODULE_SHARED
/*
 * The sip module initialisation function.
 */
#if defined(SIP_STATIC_MODULE)
PyObject *_SIP_MODULE_ENTRY(void)
#else
PyMODINIT_FUNC _SIP_MODULE_ENTRY(void)
#endif
{
    return PyModuleDef_Init(&module_def);
}
#endif


/*
 * Implement the module clear slot.
 */
// TODO This has to be exposed to be called for when the sip module is a lib.
static int module_clear(PyObject *module)
{
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(module);

    Py_CLEAR(sms->array_type);
#if defined(SIP_CONFIGURATION_PyEnums)
    Py_CLEAR(sms->builtin_int_type);
    Py_CLEAR(sms->builtin_object_type);
#endif
#if defined(SIP_CONFIGURATION_CustomEnums)
    Py_CLEAR(sms->custom_enum_type);
#endif
    Py_CLEAR(sms->base_tuple_simple_wrapper);
    Py_CLEAR(sms->base_tuple_wrapper);
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
    Py_CLEAR(sms->simple_wrapper_type);
    Py_CLEAR(sms->variable_descr_type);
    Py_CLEAR(sms->void_ptr_type);
    Py_CLEAR(sms->wrapper_type);
    Py_CLEAR(sms->wrapper_type_type);

    sipPyTypeObject *pto;

    for (pto = sms->disabled_autoconversions; pto != NULL; pto = pto->next)
        Py_CLEAR(pto->object);

    for (pto = sms->registered_py_types; pto != NULL; pto = pto->next)
        Py_CLEAR(pto->object);

    return 0;
}

/*
 * Implement the exec phase of the module initialisation.
 */
static int module_exec(PyObject *module)
{
    /* Initialise the module. */
    if (sip_init_library(module) < 0)
        return -1;

    /* Publish the bootstrap function. */
    PyObject *api_obj = PyCapsule_New((void *)bootstrap, "_C_BOOTSTRAP", NULL);

    int rc = PyModule_AddObjectRef(module, "_C_BOOTSTRAP", api_obj);
    Py_XDECREF(api_obj);

    return rc;
}


/*
 * Implement the module free slot.
 */
// TODO This has to be exposed to be called for when the sip module is a lib.
static void module_free(void *module_ptr)
{
    PyObject *module = (PyObject *)module_ptr;
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(module);

    /* Shutdown all virtual reimplementations. */
    // TODO Review the timing of this.
    sms->interpreter_state = NULL;

    module_clear(module);

    /* Handle any delayed dtors. */
    // TODO Review the timing and purpose of this.
    sipWrappedModuleDef *em;

    for (em = sms->module_list; em != NULL; em = em->em_next)
        if (em->em_ddlist != NULL)
        {
            em->em_delayeddtors(em->em_ddlist);

            /* Free the list. */
            do
            {
                sipDelayedDtor *dd = em->em_ddlist;

                em->em_ddlist = dd->dd_next;
                sip_api_free(dd);
            }
            while (em->em_ddlist != NULL);
        }

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

    /* Free the type object lists. */
    sipPyTypeObject *pto;

    pto = sms->disabled_autoconversions;
    while (pto != NULL)
    {
        sipPyTypeObject *next = pto->next;

        sip_api_free(pto);
        pto = next;
    }

    pto = sms->registered_py_types;
    while (pto != NULL)
    {
        sipPyTypeObject *next = pto->next;

        sip_api_free(pto);
        pto = next;
    }

    /* Free the object map. */
    sip_om_finalise(&sms->object_map);
}


/*
 * Implement the module traverse slot.
 */
// TODO This has to be exposed to be called for when the sip module is a lib.
static int module_traverse(PyObject *module, visitproc visit, void *arg)
{
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(module);

    Py_VISIT(sms->array_type);
#if defined(SIP_CONFIGURATION_PyEnums)
    Py_VISIT(sms->builtin_int_type);
    Py_VISIT(sms->builtin_object_type);
#endif
#if defined(SIP_CONFIGURATION_CustomEnums)
    Py_VISIT(sms->custom_enum_type);
#endif
    Py_VISIT(sms->base_tuple_simple_wrapper);
    Py_VISIT(sms->base_tuple_wrapper);
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
    Py_VISIT(sms->simple_wrapper_type);
    Py_VISIT(sms->variable_descr_type);
    Py_VISIT(sms->void_ptr_type);
    Py_VISIT(sms->wrapper_type);
    Py_VISIT(sms->wrapper_type_type);

    sipPyTypeObject *pto;

    for (pto = sms->disabled_autoconversions; pto != NULL; pto = pto->next)
        Py_VISIT(pto->object);

    for (pto = sms->registered_py_types; pto != NULL; pto = pto->next)
        Py_VISIT(pto->object);

    return 0;
}


/*
 * Return the sip module from a defining (ie. wrapped) class.
 */
PyObject *sip_get_sip_module(PyTypeObject *defining_class)
{
    return ((sipWrappedModuleState *)PyType_GetModuleState(defining_class))->wms_sip_module_interface->smh_module;
}


/*
 * Return the state for the sip module imported by a wrapped module.
 */
sipSipModuleState *sip_get_sip_module_state(PyObject *wmod)
{
    return (sipSipModuleState *)PyModule_GetState(
            ((sipWrappedModuleState *)PyModule_GetState(wmod))->wms_sip_module);
}


/*
 * Return the state for the sip module from any type.  NULL is returned if the
 * type wasn't created by the sip module.
 */
sipSipModuleState *sip_get_sip_module_state_from_any_type(PyTypeObject *type)
{
    PyObject *mod = PyType_GetModuleByDef(type, &module_def);

    if (mod == NULL)
    {
        PyErr_Clear();
        return NULL;
    }

    return (sipSipModuleState *)PyModule_GetState(mod);
}


/*
 * Return the state for the sip module from a wrapper type (or a type known to
 * be associated with the sip module).
 */
sipSipModuleState *sip_get_sip_module_state_from_wrapper_type(PyTypeObject *wt)
{
    PyObject *mod = PyType_GetModuleByDef(wt, &module_def);
    assert(mod != NULL);

    return (sipSipModuleState *)PyModule_GetState(mod);
}


/*
 * The bootstrap function.
 */
static const sipAPIDef *bootstrap(int abi_major)
{
    // TODO Verify abi_major.
    return &sip_api;
}
