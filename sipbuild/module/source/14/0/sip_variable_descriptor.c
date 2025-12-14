/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The implementation of the variable descriptor type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#include <Python.h>

#include "sip_variable_descriptor.h"

#include "sip_module.h"
#include "sip_module_wrapper.h"
#include "sip_wrapper_type.h"


/******************************************************************************
 * We don't use the similar Python descriptor because of the mixin support.
 *****************************************************************************/


/*
 * The variable descriptor data.
 */
typedef struct {
    /* The wrapped variable definition. */
    const sipWrappedVariableDef *wvd;

    /* The wrapped type containing the variable. */
    // TODO If this is a type ID (or a type number) then we should be able to
    // use these descriptors in enums and mapped types.
    PyTypeObject *w_type;

    /* The mixin name, if any. */
    PyObject *mixin_name;
} VariableDescrData;


/* Forward declarations of slots. */
static int VariableDescr_clear(PyObject *self);
static void VariableDescr_dealloc(PyObject *self);
static PyObject *VariableDescr_descr_get(PyObject *self, PyObject *obj,
        PyObject *type);
static int VariableDescr_descr_set(PyObject *self, PyObject *obj,
        PyObject *value);
static int VariableDescr_traverse(PyObject *self, visitproc visit, void *arg);


/*
 * The type specification.
 */
static PyType_Slot VariableDescr_slots[] = {
    {Py_tp_clear, VariableDescr_clear},
    {Py_tp_dealloc, VariableDescr_dealloc},
    {Py_tp_descr_get, VariableDescr_descr_get},
    {Py_tp_descr_set, VariableDescr_descr_set},
    {Py_tp_traverse, VariableDescr_traverse},
    {0, NULL}
};

static PyType_Spec VariableDescr_TypeSpec = {
    .name = _SIP_MODULE_FQ_NAME ".variabledescriptor",
    .basicsize = -(int)sizeof (VariableDescrData),
    .flags = Py_TPFLAGS_DEFAULT |
             Py_TPFLAGS_DISALLOW_INSTANTIATION |
             Py_TPFLAGS_IMMUTABLETYPE |
             Py_TPFLAGS_HAVE_GC,
    .slots = VariableDescr_slots,
};


/* Forward declarations. */
static PyObject *alloc_variable_descr(sipSipModuleState *sms);
static VariableDescrData *get_descr_data(PyObject *descr,
        sipSipModuleState *sms);


/*
 * Return a new method descriptor for the given getter/setter.
 */
PyObject *sipVariableDescr_New(sipSipModuleState *sms, PyTypeObject *w_type,
        const sipWrappedVariableDef *wvd)
{
    PyObject *descr = alloc_variable_descr(sms);
    if (descr == NULL)
        return NULL;

    VariableDescrData *descr_data = get_descr_data(descr, sms);
    if (descr_data == NULL)
    {
        Py_DECREF(descr);
        return NULL;
    }

    descr_data->wvd = wvd;
    descr_data->w_type = (PyTypeObject *)Py_NewRef(w_type);
    descr_data->mixin_name = NULL;

    return descr;
}


/*
 * Return a new variable descriptor based on an existing one and a mixin name.
 */
PyObject *sipVariableDescr_Copy(sipSipModuleState *sms, PyObject *orig,
        PyObject *mixin_name)
{
    /* Make no assumptions about the original. */
    VariableDescrData *orig_descr_data = get_descr_data(orig, NULL);
    if (orig_descr_data == NULL)
        return NULL;

    PyObject *descr = alloc_variable_descr(sms);
    if (descr == NULL)
        return NULL;

    VariableDescrData *descr_data = get_descr_data(descr, sms);
    if (descr_data == NULL)
    {
        Py_DECREF(descr);
        return NULL;
    }

    descr_data->wvd = orig_descr_data->wvd;
    descr_data->w_type = (PyTypeObject *)Py_NewRef(orig_descr_data->w_type);
    descr_data->mixin_name = Py_XNewRef(mixin_name);

    return descr;
}


/*
 * The descriptor's descriptor get slot.
 */
static PyObject *VariableDescr_descr_get(PyObject *self, PyObject *obj,
        PyObject *type)
{
    VariableDescrData *descr_data = get_descr_data(self, NULL);
    if (descr_data == NULL)
        return NULL;

    sipWrapperType *wt = (sipWrapperType *)descr_data->w_type;
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
        wt->wt_d_mod);

    return sip_variable_get(wms, obj, descr_data->wvd, descr_data->w_type,
            descr_data->mixin_name);
}


/*
 * The descriptor's descriptor set slot.
 */
static int VariableDescr_descr_set(PyObject *self, PyObject *obj,
        PyObject *value)
{
    VariableDescrData *descr_data = get_descr_data(self, NULL);
    if (descr_data == NULL)
        return -1;

    sipWrapperType *wt = (sipWrapperType *)descr_data->w_type;
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
        wt->wt_d_mod);

    return sip_variable_set(wms, obj, value, descr_data->wvd,
            descr_data->w_type, descr_data->mixin_name);
}


/*
 * The descriptor's traverse slot.
 */
static int VariableDescr_traverse(PyObject *self, visitproc visit,
        void *arg)
{
    Py_VISIT(Py_TYPE(self));

    VariableDescrData *descr_data = get_descr_data(self, NULL);

    if (descr_data != NULL)
    {
        Py_VISIT(descr_data->w_type);
        Py_VISIT(descr_data->mixin_name);
    }

    return 0;
}


/*
 * The descriptor's clear slot.
 */
static int VariableDescr_clear(PyObject *self)
{
    VariableDescrData *descr_data = get_descr_data(self, NULL);

    if (descr_data != NULL)
    {
        Py_CLEAR(descr_data->w_type);
        Py_CLEAR(descr_data->mixin_name);
    }

    return 0;
}


/*
 * The descriptor's dealloc slot.
 */
static void VariableDescr_dealloc(PyObject *self)
{
    PyObject_GC_UnTrack(self);
    VariableDescr_clear(self);
    PyTypeObject *type = Py_TYPE(self);
    type->tp_free(self);
    Py_DECREF(type);
}


/*
 * Initialise the variable descriptor support.
 */
int sip_variable_descr_init(PyObject *module, sipSipModuleState *sms)
{
    sms->variable_descr_type = (PyTypeObject *)PyType_FromModuleAndSpec(module,
            &VariableDescr_TypeSpec, NULL);

    if (sms->variable_descr_type == NULL)
        return -1;

    return 0;
}


/*
 * Allocate a new variable descriptor instance for a wrapper type.
 */
static PyObject *alloc_variable_descr(sipSipModuleState *sms)
{
    return sms->variable_descr_type->tp_alloc(sms->variable_descr_type, 0);
}


/*
 * Return the data for a descriptor instance.
 */
static VariableDescrData *get_descr_data(PyObject *descr,
        sipSipModuleState *sms)
{
    /* Get the sip module module state if necessary. */
    if (sms == NULL)
    {
        sms = sip_get_sip_module_state(Py_TYPE(descr));
        if (sms == NULL)
            return NULL;
    }

    return (VariableDescrData *)PyObject_GetTypeData(descr,
            sms->variable_descr_type);
}
