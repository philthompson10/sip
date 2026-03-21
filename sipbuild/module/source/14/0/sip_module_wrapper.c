/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This is the implementation of the sip module wrapper type.
 *
 * Copyright (c) 2026 Phil Thompson <phil@riverbankcomputing.com>
 */


#include <Python.h>

#include "sip_module_wrapper.h"

#include "sip_attribute.h"
#include "sip_core.h"
#include "sip_module.h"
#include "sip_wrapped_module.h"


/* Forward declarations of slots. */
static PyObject *ModuleWrapper_getattro(PyObject *self, PyObject *name);
static int ModuleWrapper_setattro(PyObject *self, PyObject *name,
        PyObject *value);


/*
 * The type specification.
 */
static PyType_Slot ModuleWrapper_slots[] = {
    {Py_tp_getattro, ModuleWrapper_getattro},
    {Py_tp_setattro, ModuleWrapper_setattro},
    {0, NULL}
};

static PyType_Spec ModuleWrapper_TypeSpec = {
    .name = _SIP_MODULE_FQ_NAME ".modulewrapper",
    .basicsize = 0,
    .flags = Py_TPFLAGS_DEFAULT,
    .slots = ModuleWrapper_slots,
};


/*
 * The module getattro slot.
 */
static PyObject *ModuleWrapper_getattro(PyObject *self, PyObject *name)
{
    PyObject *mod_dict = PyModule_GetDict(self);
    if (mod_dict == NULL)
        return NULL;

    sipModuleState *ms = sip_get_module_state(self);

    return sip_mod_con_getattro(ms, self, name, mod_dict,
            &ms->module_spec->attributes, &ms->module_spec->static_variables,
            NULL);
}


/*
 * The module setattro slot.
 */
static int ModuleWrapper_setattro(PyObject *self, PyObject *name,
        PyObject *value)
{
    sipModuleState *ms = sip_get_module_state(self);

    return sip_mod_con_setattro(ms, self, name, value,
            &ms->module_spec->attributes,
            &ms->module_spec->static_variables, NULL);
}


/*
 * Initialise the type.
 */
int sip_module_wrapper_init(PyObject *module, sipSipModuleState *sms)
{
    sms->module_wrapper_type = (PyTypeObject *)PyType_FromModuleAndSpec(module,
            &ModuleWrapper_TypeSpec, (PyObject *)&PyModule_Type);

    if (sms->module_wrapper_type == NULL)
        return -1;

    if (PyModule_AddType(module, sms->module_wrapper_type) < 0)
        return -1;

    return 0;
}
