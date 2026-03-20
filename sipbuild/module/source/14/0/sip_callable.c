/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The implementation of the callable type.
 *
 * Copyright (c) 2026 Phil Thompson <phil@riverbankcomputing.com>
 */


#include <Python.h>

#include <stddef.h>

#include "sip_callable.h"

#include "sip_extenders.h"
#include "sip_module.h"
#include "sip_parsers.h"


/*
 * The object data structure.
 */
typedef struct {
    PyObject_HEAD

    /* The callable specification. */
    const sipAttrSpec *attr_spec;

    /* A strong reference to the defining module. */
    PyObject *defining_module;

    /* The type specification of the containing type if it is extendable. */
    const sipTypeSpec *extending_ts;

    /* A strong reference to the optional self object. */
    PyObject *self;

    /* The vectorcall implementation. */
    vectorcallfunc vectorcall;
} CallableObject;


/* Forward declarations of slots. */
static int Callable_clear(CallableObject *self);
static void Callable_dealloc(CallableObject *self);
static int Callable_traverse(CallableObject *self, visitproc visit, void *arg);
static PyObject *Callable_vectorcall(CallableObject *self,
        PyObject *const *args, size_t nargsf, PyObject *kwd_names);


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
PyObject *sipCallable_New(sipSipModuleState *sms, const sipAttrSpec *attr_spec,
        PyObject *defining_module, PyObject *self,
        const sipTypeSpec *extending_ts)
{
    assert(attr_spec->name[0] == 'c');

    // TODO Investigate the optimisations implemented by PyCMethod, specifially
    // to reduce heap allocations.
    CallableObject *callable = (CallableObject *)PyType_GenericAlloc(
            sms->callable_type, 0);

    if (callable != NULL)
    {
        callable->attr_spec = attr_spec;
        callable->defining_module = Py_NewRef(defining_module);
        callable->self = Py_XNewRef(self);
        callable->extending_ts = extending_ts;
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
    Py_CLEAR(self->self);

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
    Py_VISIT(self->self);

    return 0;
}


/*
 * The callable's vectorcall slot.
 */
static PyObject *Callable_vectorcall(CallableObject *self,
        PyObject *const *args, size_t nargsf, PyObject *kwd_names)
{
    sipModuleState *ms = sip_get_module_state(self->defining_module);
    PyObject *p_state = NULL;
    Py_ssize_t nr_args = PyVectorcall_NARGS(nargsf);

    PyObject *res = self->attr_spec->spec.callable->callable_impl(ms, &p_state,
            self->self, args, nr_args, kwd_names);

    if (res == NULL && p_state != Py_None && self->extending_ts != NULL)
        res = sip_extend(ms, &p_state, self->self, args, nr_args, kwd_names,
                self->extending_ts, self->attr_spec->name + 1);

    if (res != NULL)
        return res;

    sip_api_no_callable(p_state, NULL, self->attr_spec->name + 1);

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
