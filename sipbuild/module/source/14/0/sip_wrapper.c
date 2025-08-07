/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The implementation of the sip wrapper type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip_wrapper.h"

#include "sip_core.h"
#include "sip_module.h"
#include "sip_simple_wrapper.h"


/* Forward declarations of slots. */
static int Wrapper_clear(PyObject *self);
static void Wrapper_dealloc(PyObject *self);
static int Wrapper_traverse(PyObject *self, visitproc visit, void *arg);


/*
 * The type specification.
 */
static PyType_Slot Wrapper_slots[] = {
    {Py_tp_clear, Wrapper_clear},
    {Py_tp_dealloc, Wrapper_dealloc},
    {Py_tp_traverse, Wrapper_traverse},
    {0, NULL}
};

static PyType_Spec Wrapper_TypeSpec = {
    .name = _SIP_MODULE_FQ_NAME ".wrapper",
    .basicsize = sizeof (sipWrapper),
    .flags = Py_TPFLAGS_DEFAULT |
             Py_TPFLAGS_BASETYPE |
             Py_TPFLAGS_HAVE_GC,
    .slots = Wrapper_slots,
};


/*
 * The wrapper clear slot.
 */
static int Wrapper_clear(PyObject *self)
{
    sipWrapper *sw = (sipWrapper *)self;
    int vret;

#if 0
    // TODO
    vret = sipSimpleWrapper_clear(self);
#else
    vret = 0;
#endif

    /* Detach any children (which will be owned by C/C++). */
    while (sw->first_child != NULL)
        sip_remove_from_parent(sw->first_child);

    return vret;
}


/*
 * The wrapper dealloc slot.
 */
static void Wrapper_dealloc(PyObject *self)
{
    /*
     * We can't simply call the super-type because things have to be done in a
     * certain order.  The first thing is to get rid of the wrapped instance.
     */
#if 0
    // TODO
    sip_forget_object((sipSimpleWrapper *)self);
#endif

    Wrapper_clear(self);

    /* Skip the super-type's dealloc. */
    PyBaseObject_Type.tp_dealloc((PyObject *)self);
}


/*
 * The wrapper traverse slot.
 */
static int Wrapper_traverse(PyObject *self, visitproc visit, void *arg)
{
    int vret;

#if 0
    // TODO
    if ((vret = sipSimpleWrapper_traverse(self, visit, arg)) != 0)
        return vret;
#endif

    sipWrapper *w;

    for (w = ((sipWrapper *)self)->first_child; w != NULL; w = w->sibling_next)
    {
        /*
         * We don't traverse if the wrapper is a child of itself.  We do this
         * so that wrapped objects returned by virtual methods with the
         * /Factory/ don't have those objects collected.  This then means that
         * plugins implemented in Python have a chance of working.
         */
        // TODO Need to collect everything.
        if ((PyObject *)w != self)
            if ((vret = visit((PyObject *)w, arg)) != 0)
                return vret;
    }

    return 0;
}


/*
 * Initialise the wrapper type.
 */
int sip_wrapper_init(PyObject *module, sipSipModuleState *sms)
{
#if PY_VERSION_HEX >= 0x030c0000
    sms->wrapper_type = (PyTypeObject *)PyType_FromMetaclass(
            sms->wrapper_type_type, module, &Wrapper_TypeSpec,
            (PyObject *)sms->simple_wrapper_type);
#else
    // TODO support for version prior to v3.12.
#endif

    if (sms->wrapper_type == NULL)
        return -1;

    if (PyModule_AddType(module, sms->wrapper_type) < 0)
        return -1;

    return 0;
}
