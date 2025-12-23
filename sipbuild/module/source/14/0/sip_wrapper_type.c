/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This is the implementation of the sip wrapper type type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#include <Python.h>

#include "sip_wrapper_type.h"

#include "sip_core.h"
#include "sip_module.h"
#include "sip_module_wrapper.h"
#include "sip_simple_wrapper.h"


/* Forward declarations of slots. */
static int WrapperType_clear(sipWrapperType *self);
static void WrapperType_dealloc(sipWrapperType *self);
static PyObject *WrapperType_getattro(sipWrapperType *self, PyObject *name);
static int WrapperType_init(sipWrapperType *self, PyObject *args,
        PyObject *kwds);
static int WrapperType_setattro(sipWrapperType *self, PyObject *name,
        PyObject *value);
static int WrapperType_traverse(sipWrapperType *self, visitproc visit,
        void *arg);


/*
 * The type specification.
 */
static PyType_Slot WrapperType_slots[] = {
    {Py_tp_clear, WrapperType_clear},
    {Py_tp_dealloc, WrapperType_dealloc},
    {Py_tp_getattro, WrapperType_getattro},
    {Py_tp_init, WrapperType_init},
    {Py_tp_setattro, WrapperType_setattro},
    {Py_tp_traverse, WrapperType_traverse},
    {0, NULL}
};

static PyType_Spec WrapperType_TypeSpec = {
    .name = _SIP_MODULE_FQ_NAME ".wrappertype",
    .basicsize = sizeof (sipWrapperType),
    .flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_HAVE_GC|
             Py_TPFLAGS_IMMUTABLETYPE|
             Py_TPFLAGS_TYPE_SUBCLASS,
    .slots = WrapperType_slots,
};


/*
 * The metatype clear slot.
 */
static int WrapperType_clear(sipWrapperType *self)
{
    Py_CLEAR(self->wt_d_mod);
    Py_CLEAR(self->wt_user_data);

    return 0;
}


/*
 * The metatype dealloc slot.
 */
static void WrapperType_dealloc(sipWrapperType *self)
{
    PyObject_GC_UnTrack((PyObject *)self);

    WrapperType_clear(self);

    PyTypeObject *type = Py_TYPE(self);
    type->tp_free(self);
    Py_DECREF(type);
}


/*
 * The metatype getattro slot.
 */
static PyObject *WrapperType_getattro(sipWrapperType *self, PyObject *name)
{
    /* Python itself may make calls along the MRO. */
    if (self->wt_d_mod == NULL)
        return PyType_Type.tp_getattro((PyObject *)self, name);

    sipModuleState *wms = (sipModuleState *)PyModule_GetState(self->wt_d_mod);
    const sipClassTypeSpec *ctd = (const sipClassTypeSpec *)sip_get_type_spec_from_wt(self);

    return sip_mod_con_getattro(wms, (PyObject *)self, name,
            &ctd->ctd_container.cod_attributes);
}


/*
 * The metatype init slot.  Note that this is *not* called for wrapped types
 * (because they are created using PyType_FromMetaclass()) but is called for
 * Python sub-classes.
 */
static int WrapperType_init(sipWrapperType *self, PyObject *args,
        PyObject *kwds)
{
    /* Call the standard super-metatype init. */
    if (PyType_Type.tp_init((PyObject *)self, args, kwds) < 0)
        return -1;

    /*
     * Disallow this being used as a meta-type for anything other than a
     * wrapped class.
     */
    sipSipModuleState *sms = sip_get_sip_module_state((PyTypeObject *)self);
    PyTypeObject *base = ((PyTypeObject *)self)->tp_base;

    if (sms == NULL || base == NULL || !PyObject_TypeCheck((PyObject *)base, sms->wrapper_type_type))
    {
        PyErr_SetString(PyExc_TypeError,
                _SIP_MODULE_FQ_NAME ".wrappertype can only be used as the "
                "metatype for wrapped classes");
        return -1;
    }

    /* Inherit from the base class. */
    self->wt_d_mod = Py_XNewRef(((sipWrapperType *)base)->wt_d_mod);
    self->wt_is_wrapper = ((sipWrapperType *)base)->wt_is_wrapper;
    self->wt_type_id = ((sipWrapperType *)base)->wt_type_id;

    /* Disallow sub-classing directly from simplewrapper or wrapper. */
    if (self->wt_d_mod == NULL)
    {
        PyErr_Format(PyExc_TypeError,
                "Python classes cannot sub-class directly from %s",
                base->tp_name);
        return -1;
    }

    self->wt_user_type = TRUE;

#if 0
    sipEventHandler *eh;

    /* Invoke any event handlers. */
    for (eh = sms->event_handlers[sipEventPySubclassCreated]; eh != NULL; eh = eh->next)
    {
        if (sipTypeIsClass(eh->td) && sip_is_subtype(wms, (const sipClassTypeSpec *)self->wt_td, (const sipClassTypeSpec *)eh->td))
        {
            sipPySubclassCreatedEventHandler handler = (sipPySubclassCreatedEventHandler)eh->handler;

            if (handler(self->wt_td, self) < 0)
                return -1;
        }
    }
#endif

    return 0;
}


/*
 * The metatype setattro slot.
 */
static int WrapperType_setattro(sipWrapperType *self, PyObject *name,
        PyObject *value)
{
    /* Python itself may make calls along the MRO. */
    if (self->wt_d_mod == NULL)
        return PyType_Type.tp_setattro((PyObject *)self, name, value);

    sipModuleState *wms = (sipModuleState *)PyModule_GetState(self->wt_d_mod);
    const sipClassTypeSpec *ctd = (const sipClassTypeSpec *)sip_get_type_spec_from_wt(self);

    return sip_mod_con_setattro(wms, (PyObject *)self, name, value,
            &ctd->ctd_container.cod_attributes);
}


/*
 * The metatype traverse slot.
 */
static int WrapperType_traverse(sipWrapperType *self, visitproc visit,
        void *arg)
{
    Py_VISIT(Py_TYPE(self));

    Py_VISIT(self->wt_d_mod);
    Py_VISIT(self->wt_user_data);

    return 0;
}


/*
 * Initialise the metatype.
 */
int sip_wrapper_type_init(PyObject *module, sipSipModuleState *sms)
{
    sms->wrapper_type_type = (PyTypeObject *)PyType_FromModuleAndSpec(module,
            &WrapperType_TypeSpec, (PyObject *)&PyType_Type);

    if (sms->wrapper_type_type == NULL)
        return -1;

    if (PyModule_AddType(module, sms->wrapper_type_type) < 0)
        return -1;

    return 0;
}
