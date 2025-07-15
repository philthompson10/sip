/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This is the implementation of the sip module wrapper type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <stdlib.h>
#include <string.h>

#include "sip_module_wrapper.h"

#include "sip_int_convertors.h"
#include "sip_module.h"


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


/* Forward declarations. */
static int compare_static_variable(const void *key, const void *el);
static const sipStaticVariableDef *get_static_variable_def(PyObject *wmod,
        PyObject *name);
static void raise_internal_error(const sipStaticVariableDef *svd);


/*
 * The type getattro slot.
 */
static PyObject *ModuleWrapper_getattro(PyObject *self, PyObject *name)
{
    const sipStaticVariableDef *svd = get_static_variable_def(self, name);

    if (svd == NULL)
        return Py_TYPE(self)->tp_base->tp_getattro(self, name);

    if (svd->getter != NULL)
        return svd->getter();

    switch (svd->type_id)
    {
        case sipTypeID_int:
            return PyLong_FromLong(*(int *)(svd->value));

        default:
            break;
    }

    raise_internal_error(svd);
    return NULL;
}


/*
 * The type setattro slot.
 */
static int ModuleWrapper_setattro(PyObject *self, PyObject *name,
        PyObject *value)
{
    const sipStaticVariableDef *svd = get_static_variable_def(self, name);

    if (svd == NULL)
        return Py_TYPE(self)->tp_base->tp_setattro(self, name, value);

    if (svd->flags & SIP_SV_RO)
    {
        PyErr_Format(PyExc_ValueError,
                "'%s' is a constant and cannot be modified", svd->name);
        return -1;
    }

    if (svd->setter != NULL)
        return svd->setter(value);

    switch (svd->type_id)
    {
        case sipTypeID_int:
        {
            int c_value = sip_api_long_as_int(value);

            if (PyErr_Occurred())
                return -1;

            *(int *)(svd->value) = c_value;

            return 0;
        }

        default:
            break;
    }

    raise_internal_error(svd);
    return -1;
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


/*
 * The bsearch() helper function for searching a static values table.
 */
static int compare_static_variable(const void *key, const void *el)
{
    return strcmp((const char *)key, ((const sipStaticVariableDef *)el)->name);
}


/*
 * Return the static value definition for a name or NULL if there was none.
 */
static const sipStaticVariableDef *get_static_variable_def(PyObject *wmod,
        PyObject *name)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);
    const sipWrappedModuleDef *wmd = wms->wrapped_module_def;

    if (wmd->nr_static_variables == 0)
        return NULL;

    return (const sipStaticVariableDef *)bsearch(
            (const void *)PyUnicode_AsUTF8(name),
            (const void *)wmd->static_variables, wmd->nr_static_variables,
            sizeof (sipStaticVariableDef), compare_static_variable);
}


/*
 * Raise an exception relating to an invalid type ID.
 */
static void raise_internal_error(const sipStaticVariableDef *svd)
{
    PyErr_Format(PyExc_SystemError, "'%s': unsupported type ID: 0x%04x",
            svd->name, svd->type_id);
}
