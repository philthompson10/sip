/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The implementation of the different descriptors.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip_core.h"


/*****************************************************************************
 * A method descriptor.  We don't use the similar Python descriptor because it
 * doesn't support a method having static and non-static overloads, and we
 * handle mixins via a delegate.
 *****************************************************************************/


/* Forward declarations of slots. */
static PyObject *sipMethodDescr_descr_get(PyObject *self, PyObject *obj,
        PyObject *type);
static PyObject *sipMethodDescr_repr(PyObject *self);
static int sipMethodDescr_traverse(PyObject *self, visitproc visit, void *arg);
static int sipMethodDescr_clear(PyObject *self);
static void sipMethodDescr_dealloc(PyObject *self);


/*
 * The object data structure.
 */
typedef struct _sipMethodDescr {
    PyObject_HEAD

    /* The method definition. */
    PyMethodDef *pmd;

    /* The containing wrapper type. */
    sipWrapperType *wt;

    /* The mixin name, if any. */
    PyObject *mixin_name;
} sipMethodDescr;


/*
 * The type specification.
 */
static PyType_Slot sipMethodDescr_slots = {
    {Py_tp_descr_get, sipMethodDescr_descr_get},
    {Py_tp_repr, sipMethodDescr_repr},
    {Py_tp_traverse, sipMethodDescr_traverse},
    {Py_tp_clear, sipMethodDescr_clear},
    {Py_tp_dealloc, sipMethodDescr_dealloc},
    {0, NULL}
};

PyType_Spec sipMethodDescr_TypeSpec = {
    .name = _SIP_MODULE_FQ_NAME ".methoddescriptor",
    .basicsize = sizeof (sipMethodDescr),
    .flags = Py_TPFLAGS_DEFAULT |
#if PY_VERSION_HEX >= 0x030a0000
             Py_TPFLAGS_IMMUTABLETYPE |
             Py_TPFLAGS_DISALLOW_INSTANTIATION |
#endif
             Py_TPFLAGS_HAVE_GC,
    .slots = sipMethodDescr_slots,
};


/* Forward declarations. */
static sipMethodDescr *alloc_method_descr(sipWrapperType *wt);


/*
 * Return a new method descriptor for the given method.
 */
PyObject *sipMethodDescr_New(PyMethodDef *pmd, sipWrapperType *wt)
{
    sipMethodDescr *descr = alloc_method_descr(wt);

    if (descr != NULL)
    {
        descr->pmd = pmd;
        descr->wt = wt;
        descr->mixin_name = NULL;

        Py_INCREF(wt);
    }

    return (PyObject *)descr;
}


/*
 * Return a new method descriptor based on an existing one and a mixin name.
 */
PyObject *sipMethodDescr_Copy(PyObject *orig, PyObject *mixin_name)
{
    sipMethodDescr *orig_descr = (sipMethodDescr *)orig;
    sipMethodDescr *descr = alloc_method_descr(orig_descr->wt);

    if (descr != NULL)
    {
        descr->vd = orig_descr->vd;
        descr->wt = orig_descr->wt;
        descr->mixin_name = mixin_name;

        Py_INCREF(orig_descr->wt);
        Py_INCREF(mixin_name);
    }

    return (PyObject *)descr;
}


/*
 * The descriptor's descriptor get slot.
 */
static PyObject *sipMethodDescr_descr_get(PyObject *self, PyObject *obj,
        PyObject *type)
{
    sipMethodDescr *descr = (sipMethodDescr *)self;
    PyObject *bind, *func;

    if (obj == NULL)
    {
        /* The argument parser must work out that 'self' is the type object. */
        bind = type;
        Py_INCREF(bind);
    }
    else if (descr->mixin_name != NULL)
    {
        bind = PyObject_GetAttr(obj, descr->mixin_name);
    }
    else
    {
        /*
         * The argument parser must work out that 'self' is the instance
         * object.
         */
        bind = obj;
        Py_INCREF(bind);
    }

    func = PyCFunction_New(descr->pmd, bind);
    Py_DECREF(bind);

    return func;
}


/*
 * The descriptor's repr slot.  This is for the benefit of cProfile which seems
 * to determine attribute names differently to the rest of Python.
 */
static PyObject *sipMethodDescr_repr(PyObject *self)
{
    sipMethodDescr *descr = (sipMethodDescr *)self;

    return PyUnicode_FromFormat("<built-in method %s>", descr->pmd->ml_name);
}


/*
 * The descriptor's traverse slot.
 */
static int sipMethodDescr_traverse(PyObject *self, visitproc visit, void *arg)
{
    sipMethodDescr *descr = (sipMethodDescr *)self;

    Py_VISIT(descr->wt);
    Py_VISIT(descr->mixin_name);

    return 0;
}


/*
 * The descriptor's clear slot.
 */
static int sipMethodDescr_clear(PyObject *self)
{
    sipMethodDescr *descr = (sipMethodDescr *)self;

    Py_CLEAR(descr->wt);
    Py_CLEAR(descr->mixin_name);

    return 0;
}


/*
 * The descriptor's dealloc slot.
 */
static void sipMethodDescr_dealloc(PyObject *self)
{
    PyObject_GC_UnTrack(self);
    sipMethodDescr_clear(self);
    PyTypeObject *type = Py_TYPE(self);
    type->tp_free(self);
    Py_DECREF(type);
}


/*
 * Allocate a new method descriptor for a wrapper type.
 */
static sipMethodDescr *alloc_method_descr(sipWrapperType *wt)
{
    sipSipModuleState *module_state = sip_get_sip_module_state(wt);

    return (sipMethodDescr *)PyType_GenericAlloc(
            module_state->sip_method_descr, 0);
}


/*****************************************************************************
 * A variable descriptor.  We don't use the similar Python descriptor because
 * it doesn't support static variables.
 *****************************************************************************/


/* Forward declarations of slots. */
static PyObject *sipVariableDescr_descr_get(PyObject *self, PyObject *obj,
        PyObject *type);
static int sipVariableDescr_descr_set(PyObject *self, PyObject *obj,
        PyObject *value);
static int sipVariableDescr_traverse(PyObject *self, visitproc visit,
        void *arg);
static int sipVariableDescr_clear(PyObject *self);
static void sipVariableDescr_dealloc(PyObject *self);


/*
 * The object data structure.
 */
typedef struct _sipVariableDescr {
    PyObject_HEAD

    /* The getter/setter definition. */
    sipVariableDef *vd;

    /* The containing wrapper type. */
    sipWrapperType *wt;

    /* The generated container name. */
    const char *cod_name;

    /* The mixin name, if any. */
    PyObject *mixin_name;
} sipVariableDescr;


/*
 * The type specification.
 */
static PyType_Slot sipVariableDescr_slots = {
    {Py_tp_descr_get, sipVariableDescr_descr_get},
    {Py_tp_descr_set, sipVariableDescr_descr_set},
    {Py_tp_traverse, sipVariableDescr_traverse},
    {Py_tp_clear, sipVariableDescr_clear},
    {Py_tp_dealloc, sipVariableDescr_dealloc},
    {0, NULL}
};

PyType_Spec sipVariableDescr_TypeSpec = {
    .name = _SIP_MODULE_FQ_NAME ".variabledescriptor",
    .basicsize = sizeof (sipVariableDescr),
    .flags = Py_TPFLAGS_DEFAULT |
#if PY_VERSION_HEX >= 0x030a0000
             Py_TPFLAGS_IMMUTABLETYPE |
             Py_TPFLAGS_DISALLOW_INSTANTIATION |
#endif
             Py_TPFLAGS_HAVE_GC,
    .slots = sipVariableDescr_slots,
};


/* Forward declarations. */
static sipVariableDescr *alloc_variable_descr(sipWrapperType *wt);
static int get_instance_address(sipVariableDescr *vd, PyObject *obj,
        void **addrp);


/*
 * Return a new method descriptor for the given getter/setter.
 */
PyObject *sipVariableDescr_New(sipVariableDef *vd, sipWrapperType *wt,
        const char *cod_name)
{
    sipVariableDescr *descr = alloc_variable_descr(wt);

    if (descr != NULL)
    {
        descr->vd = vd;
        descr->wt = wt;
        // TODO Try and get the name from the wrapper type.
        descr->cod_name = cod_name;
        descr->mixin_name = NULL;

        Py_INCREF(wt);
    }

    return (PyObject *)descr;
}


/*
 * Return a new variable descriptor based on an existing one and a mixin name.
 */
PyObject *sipVariableDescr_Copy(PyObject *orig, PyObject *mixin_name)
{
    sipVariableDescr *orig_descr = (sipVariableDescr *)orig;
    sipVariableDescr *descr = alloc_variable_descr(orig_descr->wt);

    if (descr != NULL)
    {
        descr->vd = orig_descr->vd;
        descr->wt = orig_descr->wt;
        descr->cod_name = orig_descr->cod_name;
        descr->mixin_name = mixin_name;

        Py_INCREF(orig_descr->wt);
        Py_INCREF(mixin_name);
    }

    return (PyObject *)descr;
}


/*
 * The descriptor's descriptor get slot.
 */
static PyObject *sipVariableDescr_descr_get(PyObject *self, PyObject *obj,
        PyObject *type)
{
    sipVariableDescr *descr = (sipVariableDescr *)self;
    void *addr;

    if (get_instance_address(descr, obj, &addr) < 0)
        return NULL;

    return ((sipVariableGetterFunc)descr->vd->vd_getter)(addr, obj, type);
}


/*
 * The descriptor's descriptor set slot.
 */
static int sipVariableDescr_descr_set(PyObject *self, PyObject *obj,
        PyObject *value)
{
    sipVariableDescr *descr = (sipVariableDescr *)self;
    void *addr;

    /* Check that the value isn't const. */
    if (descr->vd->vd_setter == NULL)
    {
        PyErr_Format(PyExc_AttributeError,
                "'%s' object attribute '%s' is read-only", descr->cod_name,
                descr->vd->vd_name);

        return -1;
    }

    if (get_instance_address(descr, obj, &addr) < 0)
        return -1;

    return ((sipVariableSetterFunc)descr->vd->vd_setter)(addr, value, obj);
}


/*
 * The descriptor's traverse slot.
 */
static int sipVariableDescr_traverse(PyObject *self, visitproc visit, void *arg)
{
    sipVariableDescr *descr = (sipVariableDescr *)self;

    Py_VISIT(descr->wt);
    Py_VISIT(descr->mixin_name);

    return 0;
}


/*
 * The descriptor's clear slot.
 */
static int sipVariableDescr_clear(PyObject *self)
{
    sipVariableDescr *descr = (sipVariableDescr *)self;

    Py_CLEAR(descr->wt);
    Py_CLEAR(descr->mixin_name);

    return 0;
}


/*
 * The descriptor's dealloc slot.
 */
static void sipVariableDescr_dealloc(PyObject *self)
{
    PyObject_GC_UnTrack(self);
    sipVariableDescr_clear(self);
    PyTypeObject *type = Py_TYPE(self);
    type->tp_free(self);
    Py_DECREF(type);
}


/*
 * Allocate a new variable descriptor for a wrapper type.
 */
static sipVariableDescr *alloc_variable_descr(sipWrapperType *wt)
{
    sipSipModuleState *module_state = sip_get_sip_module_state(wt);

    return (sipVariableDescr *)PyType_GenericAlloc(
            module_state->sip_variable_descr, 0);
}


/*
 * Return the C/C++ address of any instance.
 */
static int get_instance_address(sipVariableDescr *descr, PyObject *obj,
        void **addrp)
{
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
        if ((addr = sip_api_get_cpp_ptr((sipSimpleWrapper *)obj, descr->wt->wt_td)) == NULL)
            return -1;
    }

    *addrp = addr;

    return 0;
}
