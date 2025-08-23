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


/******************************************************************************
 * We don't use the similar Python descriptor because it doesn't support static
 * variables.
 *****************************************************************************/


/*
 * The object data structure.
 */
typedef struct {
    PyObject_HEAD

    /* The getter/setter definition. */
    const sipVariableDef *vd;

    /* The containing wrapper type definition. */
    const sipTypeDef *td;

    /* The generated container name.  This is only used for error messages. */
    const char *cod_name;

    /* The mixin name, if any. */
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
static int get_instance_address(VariableDescr *vd, PyObject *obj,
        void **addrp);


/*
 * Return a new method descriptor for the given getter/setter.
 */
PyObject *sipVariableDescr_New(sipSipModuleState *sms,
        const sipVariableDef *vd, const sipTypeDef *td, const char *cod_name)
{
    VariableDescr *descr = alloc_variable_descr(sms);

    if (descr != NULL)
    {
        descr->vd = vd;
        descr->td = td;
        descr->cod_name = cod_name;
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
        descr->vd = orig_descr->vd;
        descr->td = orig_descr->td;
        descr->cod_name = orig_descr->cod_name;
        descr->mixin_name = mixin_name;

        Py_INCREF(mixin_name);
    }

    return (PyObject *)descr;
}


/*
 * The descriptor's descriptor get slot.
 */
static PyObject *VariableDescr_descr_get(VariableDescr *self, PyObject *obj,
        PyObject *type)
{
    void *addr;

    if (get_instance_address(self, obj, &addr) < 0)
        return NULL;

    return ((sipVariableGetterFunc)self->vd->vd_getter)(addr, obj, type);
}


/*
 * The descriptor's descriptor set slot.
 */
static int VariableDescr_descr_set(VariableDescr *self, PyObject *obj,
        PyObject *value)
{
    void *addr;

    /* Check that the value isn't const. */
    if (self->vd->vd_setter == NULL)
    {
        PyErr_Format(PyExc_AttributeError,
                "'%s' object attribute '%s' is read-only", self->cod_name,
                self->vd->vd_name);

        return -1;
    }

    if (get_instance_address(self, obj, &addr) < 0)
        return -1;

    return ((sipVariableSetterFunc)self->vd->vd_setter)(addr, value, obj);
}


/*
 * The descriptor's traverse slot.
 */
static int VariableDescr_traverse(VariableDescr *self, visitproc visit,
        void *arg)
{
    Py_VISIT(Py_TYPE(self));
    Py_VISIT(self->mixin_name);

    return 0;
}


/*
 * The descriptor's clear slot.
 */
static int VariableDescr_clear(VariableDescr *self)
{
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


/*
 * Return the C/C++ address of any instance.
 */
static int get_instance_address(VariableDescr *descr, PyObject *obj,
        void **addrp)
{
#if 0
    void *addr;

    if (descr->vd->vd_type == ClassVariable)
    {
        addr = NULL;
    }
    else
    {
        /* Check that access was via an instance. */
        if (obj == NULL || obj == Py_None)
        {
            PyErr_Format(PyExc_AttributeError,
                    "'%s' object attribute '%s' is an instance attribute",
                    descr->cod_name, descr->vd->vd_name);

            return -1;
        }

        if (descr->mixin_name != NULL)
            obj = PyObject_GetAttr(obj, descr->mixin_name);

        /* Get the C++ instance. */
        if ((addr = sip_api_get_cpp_ptr((sipSimpleWrapper *)obj, descr->td)) == NULL)
            return -1;
    }

    *addrp = addr;

    return 0;
#else
    return -1;
#endif
}
