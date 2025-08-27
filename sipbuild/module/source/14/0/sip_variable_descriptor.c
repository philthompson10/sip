/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The implementation of the variable descriptor type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip_variable_descriptor.h"

#include "sip_module.h"
#include "sip_module_wrapper.h"
#include "sip_wrapper_type.h"


/******************************************************************************
 * We don't use the similar Python descriptor because of the mixin support.
 *****************************************************************************/


/*
 * The object data structure.
 */
typedef struct {
    PyObject_HEAD

    /* The wrapped variable definition. */
    const sipWrappedVariableDef *wvd;

    /* The wrapped type containing the variable. */
    // TODO If this is a type ID (or a type number) then we should be able to
    // use these descriptors in enums and mapped types.
    sipWrapperType *type;

    /* The mixin name, if any. */
    // TODO Review the mixin support, specifically for static class variables.
    // TODO Should this be a type ID that is part of sipWrappedVariableDef?
    PyObject *mixin_name;
} VariableDescr;


/* Forward declarations of slots. */
static int VariableDescr_clear(VariableDescr *self);
static void VariableDescr_dealloc(VariableDescr *self);
static PyObject *VariableDescr_descr_get(VariableDescr *self, PyObject *obj,
        PyObject *type);
static int VariableDescr_descr_set(VariableDescr *self, PyObject *obj,
        PyObject *value);
static int VariableDescr_traverse(VariableDescr *self, visitproc visit,
        void *arg);


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
    .basicsize = sizeof (VariableDescr),
    .flags = Py_TPFLAGS_DEFAULT |
#if defined(Py_TPFLAGS_DISALLOW_INSTANTIATION)
             Py_TPFLAGS_DISALLOW_INSTANTIATION |
#endif
#if defined(Py_TPFLAGS_IMMUTABLETYPE)
             Py_TPFLAGS_IMMUTABLETYPE |
#endif
             Py_TPFLAGS_HAVE_GC,
    .slots = VariableDescr_slots,
};


/* Forward declarations. */
static VariableDescr *alloc_variable_descr(sipSipModuleState *sms);


/*
 * Return a new method descriptor for the given getter/setter.
 */
PyObject *sipVariableDescr_New(sipSipModuleState *sms, sipWrapperType *type,
        const sipWrappedVariableDef *wvd)
{
    VariableDescr *descr = alloc_variable_descr(sms);

    if (descr != NULL)
    {
        descr->wvd = wvd;
        descr->type = (sipWrapperType *)Py_NewRef(type);
        descr->mixin_name = NULL;
    }

    return (PyObject *)descr;
}


/*
 * Return a new variable descriptor based on an existing one and a mixin name.
 */
PyObject *sipVariableDescr_Copy(sipSipModuleState *sms, PyObject *orig,
        PyObject *mixin_name)
{
    VariableDescr *orig_descr = (VariableDescr *)orig;
    VariableDescr *descr = alloc_variable_descr(sms);

    if (descr != NULL)
    {
        descr->wvd = orig_descr->wvd;
        descr->type = (sipWrapperType *)Py_NewRef(orig_descr->type);
        descr->mixin_name = Py_NewRef(mixin_name);
    }

    return (PyObject *)descr;
}


/*
 * The descriptor's descriptor get slot.
 */
static PyObject *VariableDescr_descr_get(VariableDescr *self, PyObject *obj,
        PyObject *type)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
        self->type->wt_dmod);

    return sip_variable_get(wms, obj, self->wvd, self->type, self->mixin_name);
}


/*
 * The descriptor's descriptor set slot.
 */
static int VariableDescr_descr_set(VariableDescr *self, PyObject *obj,
        PyObject *value)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
        self->type->wt_dmod);

    return sip_variable_set(wms, obj, value, self->wvd, self->type,
            self->mixin_name);
}


/*
 * The descriptor's traverse slot.
 */
static int VariableDescr_traverse(VariableDescr *self, visitproc visit,
        void *arg)
{
    Py_VISIT(Py_TYPE(self));
    Py_VISIT(self->type);
    Py_VISIT(self->mixin_name);

    return 0;
}


/*
 * The descriptor's clear slot.
 */
static int VariableDescr_clear(VariableDescr *self)
{
    Py_CLEAR(self->type);
    Py_CLEAR(self->mixin_name);

    return 0;
}


/*
 * The descriptor's dealloc slot.
 */
static void VariableDescr_dealloc(VariableDescr *self)
{
    PyObject_GC_UnTrack((PyObject *)self);
    VariableDescr_clear(self);
    PyTypeObject *type = Py_TYPE(self);
    type->tp_free(self);
    Py_DECREF(type);
}


/*
 * Initialise the variable descriptor.
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
 * Allocate a new variable descriptor for a wrapper type.
 */
static VariableDescr *alloc_variable_descr(sipSipModuleState *sms)
{
    return (VariableDescr *)PyType_GenericAlloc(sms->variable_descr_type, 0);
}
