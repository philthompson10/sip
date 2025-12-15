/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The implementation of the method descriptor type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#include <Python.h>

#include "sip_method_descriptor.h"

#include "sip_module.h"


/******************************************************************************
 * We don't use the similar Python descriptor because it doesn't support a
 * method having static and non-static overloads, and we handle mixins via a
 * delegate.
 *****************************************************************************/


/*
 * The method descriptor data.
 */
typedef struct {
    /* The method definition. */
    const PyMethodDef *pmd;

    /* The defining class. */
    PyTypeObject *defining_class;

    /* The mixin name, if any. */
    PyObject *mixin_name;
} MethodDescrData;


/* Forward declarations of slots. */
static int MethodDescr_clear(PyObject *self);
static void MethodDescr_dealloc(PyObject *self);
static PyObject *MethodDescr_descr_get(PyObject *self, PyObject *obj,
        PyObject *type);
static PyObject *MethodDescr_repr(PyObject *self);
static int MethodDescr_traverse(PyObject *self, visitproc visit, void *arg);


/*
 * The type specification.
 */
static PyType_Slot MethodDescr_slots[] = {
    {Py_tp_clear, MethodDescr_clear},
    {Py_tp_dealloc, MethodDescr_dealloc},
    {Py_tp_descr_get, MethodDescr_descr_get},
    {Py_tp_repr, MethodDescr_repr},
    {Py_tp_traverse, MethodDescr_traverse},
    {0, NULL}
};

static PyType_Spec MethodDescr_TypeSpec = {
    .name = _SIP_MODULE_FQ_NAME ".methoddescriptor",
    .basicsize = -(int)sizeof (MethodDescrData),
    .flags = Py_TPFLAGS_DEFAULT |
             Py_TPFLAGS_DISALLOW_INSTANTIATION |
             Py_TPFLAGS_IMMUTABLETYPE |
             Py_TPFLAGS_HAVE_GC,
    .slots = MethodDescr_slots,
};


/* Forward declarations. */
static PyObject *alloc_method_descr(sipSipModuleState *sms);
static MethodDescrData *get_descr_data(PyObject *descr,
        sipSipModuleState *sms);


/*
 * Return a new method descriptor for the given method.
 */
PyObject *sipMethodDescr_New(sipSipModuleState *sms, const PyMethodDef *pmd,
        PyTypeObject *defining_class)
{
    PyObject *descr = alloc_method_descr(sms);
    if (descr == NULL)
        return NULL;

    MethodDescrData *descr_data = get_descr_data(descr, sms);
    if (descr_data == NULL)
    {
        Py_DECREF(descr);
        return NULL;
    }

    descr_data->pmd = pmd;
    descr_data->defining_class = defining_class;
    descr_data->mixin_name = NULL;

    return descr;
}


/*
 * Return a new method descriptor based on an existing one and a mixin name.
 */
PyObject *sipMethodDescr_Copy(sipSipModuleState *sms, PyObject *orig,
        PyObject *mixin_name)
{
    /* Make no assumptions about the original. */
    MethodDescrData *orig_descr_data = get_descr_data(orig, NULL);
    if (orig_descr_data == NULL)
        return NULL;

    PyObject *descr = alloc_method_descr(sms);
    if (descr == NULL)
        return NULL;

    MethodDescrData *descr_data = get_descr_data(descr, sms);
    if (descr_data == NULL)
    {
        Py_DECREF(descr);
        return NULL;
    }

    descr_data->pmd = orig_descr_data->pmd;
    descr_data->defining_class = (PyTypeObject *)Py_NewRef(
            orig_descr_data->defining_class);
    descr_data->mixin_name = Py_XNewRef(mixin_name);

    return descr;
}


/*
 * The descriptor's descriptor get slot.
 */
static PyObject *MethodDescr_descr_get(PyObject *self, PyObject *obj,
        PyObject *type)
{
    MethodDescrData *descr_data = get_descr_data(self, NULL);
    if (descr_data == NULL)
        return NULL;

    PyObject *bind;

    if (obj == NULL)
    {
        /* The argument parser must work out that 'self' is the type object. */
        bind = Py_NewRef(type);
    }
    else if (descr_data->mixin_name != NULL)
    {
        bind = PyObject_GetAttr(obj, descr_data->mixin_name);
    }
    else
    {
        /*
         * The argument parser must work out that 'self' is the instance
         * object.
         */
        bind = Py_NewRef(obj);
    }

    PyObject *func = PyCMethod_New((PyMethodDef *)descr_data->pmd, bind, NULL,
            descr_data->defining_class);
    Py_DECREF(bind);

    return func;
}


/*
 * The descriptor's repr slot.  This is for the benefit of cProfile which seems
 * to determine attribute names differently to the rest of Python.
 */
static PyObject *MethodDescr_repr(PyObject *self)
{
    MethodDescrData *descr_data = get_descr_data(self, NULL);
    if (descr_data == NULL)
        return NULL;

    return PyUnicode_FromFormat("<built-in method %s>",
            descr_data->pmd->ml_name);
}


/*
 * The descriptor's traverse slot.
 */
static int MethodDescr_traverse(PyObject *self, visitproc visit, void *arg)
{
    Py_VISIT(Py_TYPE(self));

    MethodDescrData *descr_data = get_descr_data(self, NULL);

    if (descr_data != NULL)
    {
        Py_VISIT(descr_data->defining_class);
        Py_VISIT(descr_data->mixin_name);
    }

    return 0;
}


/*
 * The descriptor's clear slot.
 */
static int MethodDescr_clear(PyObject *self)
{
    MethodDescrData *descr_data = get_descr_data(self, NULL);

    if (descr_data != NULL)
    {
        Py_CLEAR(descr_data->defining_class);
        Py_CLEAR(descr_data->mixin_name);
    }

    return 0;
}


/*
 * The descriptor's dealloc slot.
 */
static void MethodDescr_dealloc(PyObject *self)
{
    PyObject_GC_UnTrack(self);
    MethodDescr_clear(self);
    PyTypeObject *type = Py_TYPE(self);
    type->tp_free(self);
    Py_DECREF(type);
}


/*
 * Initialise the method descriptor.
 */
int sip_method_descr_init(PyObject *module, sipSipModuleState *sms)
{
    sms->method_descr_type = (PyTypeObject *)PyType_FromModuleAndSpec(module,
            &MethodDescr_TypeSpec, NULL);

    if (sms->method_descr_type == NULL)
        return -1;

    return 0;
}


/*
 * Allocate a new method descriptor object for a wrapper type.
 */
static PyObject *alloc_method_descr(sipSipModuleState *sms)
{
    return sms->method_descr_type->tp_alloc(sms->method_descr_type, 0);
}


/*
 * Return the data for a descriptor instance.
 */
static MethodDescrData *get_descr_data(PyObject *descr, sipSipModuleState *sms)
{
    /* Get the sip module module state if necessary. */
    if (sms == NULL)
    {
        sms = sip_get_sip_module_state(Py_TYPE(descr));
        if (sms == NULL)
            return NULL;
    }

    return (MethodDescrData *)PyObject_GetTypeData(descr,
            sms->method_descr_type);
}
