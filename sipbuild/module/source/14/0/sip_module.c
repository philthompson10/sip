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
#if defined(SIP_CONFIGURATION_CustomEnums)
    Py_CLEAR(sms->custom_enum_type);
#endif
    Py_CLEAR(sms->base_tuple_simple_wrapper);
    Py_CLEAR(sms->base_tuple_wrapper);
    Py_CLEAR(sms->method_descr_type);
    Py_CLEAR(sms->simple_wrapper_type);
    Py_CLEAR(sms->variable_descr_type);
    Py_CLEAR(sms->void_ptr_type);
    Py_CLEAR(sms->wrapper_type);
    Py_CLEAR(sms->wrapper_type_type);

    return 0;
}

/*
 * Implement the exec phase of the module initialisation.
 */
static int module_exec(PyObject *module)
{
    /* Initialise the module. */
    const sipAPI *api = sip_init_library(module);

    if (api == NULL)
        return -1;

    /* Publish the SIP API. */
    PyObject *api_obj = PyCapsule_New((void *)api,
            _SIP_MODULE_FQ_NAME "._C_API", NULL);

    // TODO Add the bootstrap rather than the API structure.
    int rc = PyModule_AddObjectRef(module, "_C_API", api_obj);

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

    module_clear(module);

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
#if defined(SIP_CONFIGURATION_CustomEnums)
    Py_VISIT(sms->custom_enum_type);
#endif
    Py_VISIT(sms->base_tuple_simple_wrapper);
    Py_VISIT(sms->base_tuple_wrapper);
    Py_VISIT(sms->method_descr_type);
    Py_VISIT(sms->simple_wrapper_type);
    Py_VISIT(sms->variable_descr_type);
    Py_VISIT(sms->void_ptr_type);
    Py_VISIT(sms->wrapper_type);
    Py_VISIT(sms->wrapper_type_type);

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
            ((sipWrappedModuleState *)PyModule_GetState(wmod))->wms_sip_module_interface->smh_module);
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
 * Return the state for the sip module from a wrapper.
 */
sipSipModuleState *sip_get_sip_module_state_from_wrapper(PyObject *wrapper)
{
    PyObject *mod = PyType_GetModuleByDef(Py_TYPE(wrapper), &module_def);
    assert(mod != NULL);

    return (sipSipModuleState *)PyModule_GetState(mod);
}
