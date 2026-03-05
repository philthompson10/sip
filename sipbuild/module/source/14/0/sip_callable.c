/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The implementation of the callable type.
 *
 * Copyright (c) 2026 Phil Thompson <phil@riverbankcomputing.com>
 */


#include <Python.h>

#include <stddef.h>

#include "sip_callable.h"

#include "sip_module.h"
#include "sip_parsers.h"


/*
 * The object data structure.
 */
typedef struct {
    PyObject_HEAD

    /* The callable specification. */
    const sipCallableSpec *c_spec;

    /* A strong reference to the defining module. */
    PyObject *defining_module;

    /* The vectorcall implementation. */
    vectorcallfunc vectorcall;
} CallableObject;


/* Forward declarations of slots. */
static int Callable_clear(CallableObject *self);
static void Callable_dealloc(CallableObject *self);
static int Callable_traverse(CallableObject *self, visitproc visit, void *arg);
static PyObject *Callable_vectorcall(CallableObject *self,
        PyObject *const *args, size_t nargsf, PyObject *kwnames);


/*
 * The type specification.
 */
static PyMemberDef Callable_members[] = {
    {"__vectorcalloffset__", Py_T_PYSSIZET, offsetof(CallableObject, vectorcall), Py_READONLY},
    {}
};

static PyType_Slot Callable_slots[] = {
    {Py_tp_call, PyVectorcall_Call},
    {Py_tp_clear, Callable_clear},
    {Py_tp_dealloc, Callable_dealloc},
    {Py_tp_members, Callable_members},
    {Py_tp_traverse, Callable_traverse},
    {}
};

static PyType_Spec Callable_TypeSpec = {
    .name = _SIP_MODULE_FQ_NAME ".callable",
    .basicsize = sizeof (CallableObject),
    .flags = Py_TPFLAGS_DEFAULT |
             Py_TPFLAGS_DISALLOW_INSTANTIATION |
             Py_TPFLAGS_IMMUTABLETYPE |
             Py_TPFLAGS_HAVE_GC |
             Py_TPFLAGS_HAVE_VECTORCALL,
    .slots = Callable_slots,
};


/*
 * Return a new callable.
 */
PyObject *sipCallable_New(sipSipModuleState *sms,
        const sipCallableSpec *c_spec, PyObject *defining_module)
{
    // TODO Investigate the optimisations implemented by PyCMethod, specifially
    // to reduce heap allocations.  Don't bother if we can get rid of the
    // custom method descriptor.
    CallableObject *callable = (CallableObject *)PyType_GenericAlloc(
            sms->callable_type, 0);

    if (callable != NULL)
    {
        callable->c_spec = c_spec;
        callable->defining_module = Py_NewRef(defining_module);
        callable->vectorcall = (vectorcallfunc)Callable_vectorcall;
    }

    return (PyObject *)callable;
}


/*
 * The callable's clear slot.
 */
static int Callable_clear(CallableObject *self)
{
    Py_CLEAR(self->defining_module);

    return 0;
}


/*
 * The callable's dealloc slot.
 */
static void Callable_dealloc(CallableObject *self)
{
    PyObject_GC_UnTrack((PyObject *)self);
    Callable_clear(self);
    PyTypeObject *type = Py_TYPE(self);
    type->tp_free(self);
    Py_DECREF(type);
}


/*
 * The callable's traverse slot.
 */
static int Callable_traverse(CallableObject *self, visitproc visit, void *arg)
{
    Py_VISIT(Py_TYPE(self));
    Py_VISIT(self->defining_module);

    return 0;
}


/*
 * The callable's vectorcall slot.
 */
static PyObject *Callable_vectorcall(CallableObject *self,
        PyObject *const *args, size_t nargsf, PyObject *kwnames)
{
    // TODO Handle things other than module callables.
    PyObject *p_state = NULL;

    PyObject *res = self->c_spec->callable_impl(self->defining_module,
            &p_state, NULL, false, args, PyVectorcall_NARGS(nargsf), kwnames);

    if (res != NULL)
        return res;

    sip_no_callable(p_state, NULL, self->c_spec->name);

    return NULL;
}


/*
 * Initialise the callable type.
 */
int sip_callable_init(PyObject *module, sipSipModuleState *sms)
{
    sms->callable_type = (PyTypeObject *)PyType_FromModuleAndSpec(module,
            &Callable_TypeSpec, NULL);

    if (sms->callable_type == NULL)
        return -1;

    return 0;
}
