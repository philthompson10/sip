/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The implementation of the method descriptor type.
 *
 * Copyright (c) 2026 Phil Thompson <phil@riverbankcomputing.com>
 */


#include <Python.h>

#include "sip_method_descriptor.h"

#include "sip_callable.h"
#include "sip_module.h"


/******************************************************************************
 * We don't use the similar Python descriptor because it doesn't support a
 * method having static and non-static overloads, and we handle mixins via a
 * delegate.
 *****************************************************************************/


/*
 * The object data structure.
 */
typedef struct {
    PyObject_HEAD

    /* The callable specification. */
    const sipCallableSpec *c_spec;

    /* A strong reference to the defining module. */
    PyObject *defining_module;

    /* The type specification of the containing type if it is extendable. */
    const sipTypeSpec *extending_ts;

    /* The mixin name, if any. */
    PyObject *mixin_name;
} MethodDescr;


/* Forward declarations of slots. */
static int MethodDescr_clear(MethodDescr *self);
static void MethodDescr_dealloc(MethodDescr *self);
static PyObject *MethodDescr_descr_get(MethodDescr *self, PyObject *obj,
        PyObject *type);
static PyObject *MethodDescr_repr(MethodDescr *self);
static int MethodDescr_traverse(MethodDescr *self, visitproc visit, void *arg);


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
    .basicsize = sizeof (MethodDescr),
    .flags = Py_TPFLAGS_DEFAULT |
             Py_TPFLAGS_DISALLOW_INSTANTIATION |
             Py_TPFLAGS_IMMUTABLETYPE |
             Py_TPFLAGS_HAVE_GC,
    .slots = MethodDescr_slots,
};


/* Forward declarations. */
static MethodDescr *alloc_method_descr(sipSipModuleState *sms);


/*
 * Return a new method descriptor for the given method.
 */
PyObject *sipMethodDescr_New(sipSipModuleState *sms,
        const sipCallableSpec *c_spec, PyObject *defining_module,
        const sipTypeSpec *extending_ts)
{
    MethodDescr *descr = alloc_method_descr(sms);

    if (descr != NULL)
    {
        descr->c_spec = c_spec;
        descr->defining_module = Py_NewRef(defining_module);
        descr->extending_ts = extending_ts;
        descr->mixin_name = NULL;
    }

    return (PyObject *)descr;
}


/*
 * Return a new method descriptor based on an existing one and a mixin name.
 */
PyObject *sipMethodDescr_Copy(sipSipModuleState *sms, PyObject *orig,
        PyObject *mixin_name)
{
    MethodDescr *orig_descr = (MethodDescr *)orig;
    MethodDescr *descr = alloc_method_descr(sms);

    if (descr != NULL)
    {
        descr->c_spec = orig_descr->c_spec;
        descr->defining_module = Py_NewRef(orig_descr->defining_module);
        descr->extending_ts = orig_descr->extending_ts;
        descr->mixin_name = Py_XNewRef(mixin_name);
    }

    return (PyObject *)descr;
}


/*
 * The descriptor's descriptor get slot.
 */
static PyObject *MethodDescr_descr_get(MethodDescr *self, PyObject *obj,
        PyObject *type)
{
    sipModuleState *ms = (sipModuleState *)PyModule_GetState(
            self->defining_module);
    if (ms == NULL)
        return NULL;

    // TODO We could tell the callable about the nature of 'self' (mixin?) but
    // for the moment we use the legacy introspection in the parser.
    PyObject *bind;

    if (obj == NULL)
    {
        /* The argument parser must work out that 'self' is the type object. */
        bind = Py_NewRef(type);
    }
    else if (self->mixin_name != NULL)
    {
        bind = PyObject_GetAttr(obj, self->mixin_name);
    }
    else
    {
        /*
         * The argument parser must work out that 'self' is the instance
         * object.
         */
        bind = Py_NewRef(obj);
    }

    PyObject *callable = sipCallable_New(ms->sip_module_state, self->c_spec,
            self->defining_module, bind, self->extending_ts);
    Py_DECREF(bind);

    return callable;
}


/*
 * The descriptor's repr slot.  This is for the benefit of cProfile which seems
 * to determine attribute names differently to the rest of Python.
 */
static PyObject *MethodDescr_repr(MethodDescr *self)
{
    return PyUnicode_FromFormat("<built-in method %s>", self->c_spec->name);
}


/*
 * The descriptor's traverse slot.
 */
static int MethodDescr_traverse(MethodDescr *self, visitproc visit, void *arg)
{
    Py_VISIT(Py_TYPE(self));
    Py_VISIT(self->defining_module);
    Py_VISIT(self->mixin_name);

    return 0;
}


/*
 * The descriptor's clear slot.
 */
static int MethodDescr_clear(MethodDescr *self)
{
    Py_CLEAR(self->defining_module);
    Py_CLEAR(self->mixin_name);

    return 0;
}


/*
 * The descriptor's dealloc slot.
 */
static void MethodDescr_dealloc(MethodDescr *self)
{
    PyObject_GC_UnTrack((PyObject *)self);
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
 * Allocate a new method descriptor for a wrapper type.
 */
static MethodDescr *alloc_method_descr(sipSipModuleState *sms)
{
    return (MethodDescr *)PyType_GenericAlloc(sms->method_descr_type, 0);
}
