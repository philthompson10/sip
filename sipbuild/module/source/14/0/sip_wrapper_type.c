/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This is the implementation of the sip wrapper type type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip_wrapper_type.h"

#include "sip_container.h"
#include "sip_core.h"
#include "sip_module.h"
#include "sip_simple_wrapper.h"


/* Forward declarations of slots. */
#if 0
static PyObject *WrapperType_alloc(PyTypeObject *self, Py_ssize_t nitems);
static void WrapperType_dealloc(PyObject *self);
static PyObject *WrapperType_getattro(PyObject *self, PyObject *name);
#endif
//static int WrapperType_init(sipWrapperType *self, PyObject *args,
//        PyObject *kwds);
#if 0
static int WrapperType_setattro(PyObject *self, PyObject *name,
        PyObject *value);
static int WrapperType_traverse(PyObject *self, visitproc visit, void *arg);
#endif


/*
 * The type specification.
 */
static PyType_Slot WrapperType_slots[] = {
#if 0
    {Py_tp_alloc, WrapperType_alloc},
    {Py_tp_dealloc, WrapperType_dealloc},
    {Py_tp_getattro, WrapperType_getattro},
#endif
//    {Py_tp_init, WrapperType_init},
#if 0
    {Py_tp_setattro, WrapperType_setattro},
    {Py_tp_traverse, WrapperType_traverse},
#endif
    {0, NULL}
};

static PyType_Spec WrapperType_TypeSpec = {
    .name = _SIP_MODULE_FQ_NAME ".wrappertype",
    .basicsize = sizeof (sipWrapperType),
    .flags = Py_TPFLAGS_DEFAULT |
#if defined(Py_TPFLAGS_IMMUTABLETYPE)
             Py_TPFLAGS_IMMUTABLETYPE |
#endif
             Py_TPFLAGS_BASETYPE |
             Py_TPFLAGS_TYPE_SUBCLASS,
    .slots = WrapperType_slots,
};


#if 0
/*
 * The metatype alloc slot.
 */
static PyObject *WrapperType_alloc(PyTypeObject *self, Py_ssize_t nitems)
{
    PyObject *o;

    /* Call the standard super-metatype alloc. */
    if ((o = PyType_Type.tp_alloc(self, nitems)) == NULL)
        return NULL;

    /*
     * Consume any extra type specific information and use it to initialise the
     * slots.  This only happens for directly wrapped classes (and not
     * programmer written sub-classes).  This must be done in the alloc
     * function because it is the only place we can break out of the default
     * new() function before PyType_Ready() is called.
     */
#if 0
Try and get rid of the back door.
     sipSipModuleState *sms = sip_get_sip_module_state_from_wrapper_type(self);

    if (sms->current_type_def_backdoor != NULL)
    {
        assert(!sipTypeIsEnum(sms->current_type_def_backdoor));

        ((sipWrapperType *)o)->wt_td = sms->current_type_def_backdoor;

        if (sipTypeIsClass(sms->current_type_def_backdoor) || sipTypeIsNamespace(sms->current_type_def_backdoor))
        {
            const sipClassTypeDef *ctd = (const sipClassTypeDef *)sms->current_type_def_backdoor;
            const char *docstring = ctd->ctd_docstring;

            /*
             * Skip the marker that identifies the docstring as being
             * automatically generated.
             */
            if (docstring != NULL && *docstring == AUTO_DOCSTRING)
                ++docstring;

            ((PyTypeObject *)o)->tp_doc = docstring;

            PyHeapTypeObject *heap_to = &(((sipWrapperType *)o)->super);
            PyBufferProcs *bp = &heap_to->as_buffer;

            /* Add the buffer interface. */
            if (ctd->ctd_getbuffer != NULL)
                bp->bf_getbuffer = (getbufferproc)sipSimpleWrapper_getbuffer;

            if (ctd->ctd_releasebuffer != NULL)
                bp->bf_releasebuffer = (releasebufferproc)sipSimpleWrapper_releasebuffer;

            /* Add the slots for this type. */
            if (ctd->ctd_pyslots != NULL)
                sip_add_type_slots(heap_to, ctd->ctd_pyslots);

            /* Patch any mixin initialiser. */
            if (ctd->ctd_init_mixin != NULL)
                ((PyTypeObject *)o)->tp_init = ctd->ctd_init_mixin;
        }
    }
#endif

    return o;
}


/*
 * The metatype dealloc slot.
 */
static void WrapperType_dealloc(PyObject *self)
{
    PyObject_GC_UnTrack(self);
    PyTypeObject *type = Py_TYPE(self);
    type->tp_free(self);
    Py_DECREF(type);
}


/*
 * The metatype getattro slot.
 */
static PyObject *WrapperType_getattro(PyObject *self, PyObject *name)
{
#if 0
    sipSipModuleState *sms = sip_get_sip_module_state_from_wrapper_type(
            (PyTypeObject *)self);

    if (sip_container_add_lazy_attrs(wms, ((sipWrapperType *)self)->wt_td) < 0)
        return NULL;
#endif

    return PyType_Type.tp_getattro(self, name);
}
#endif


#if 0
/*
 * The metatype init slot.  Note that this is *not* called for wrapped types
 * (because they are created using PyType_FromMetaclass()) but is called for
 * user-defined sub-classes of wrapped types.
 */
static int WrapperType_init(sipWrapperType *self, PyObject *args,
        PyObject *kwds)
{
    printf("++++++ in WrapperType_init() for %p\n", self);
    /* Call the standard super-metatype init. */
    if (PyType_Type.tp_init((PyObject *)self, args, kwds) < 0)
        return -1;

#if 0
May need to keep this just for the event handler.  Is it still needed?  Is
there a better way?
    /*
     * If we don't yet have any extra type specific information (because we are
     * a programmer defined sub-class) then get it from the (first) super-type.
     */
    if (self->wt_td == NULL)
    {
        sipSipModuleState *sms = sip_get_sip_module_state_from_wrapper_type(
                (PyTypeObject *)self);
        PyTypeObject *base = ((PyTypeObject *)self)->tp_base;

        self->wt_user_type = TRUE;

        /*
         * We allow the class to use this as a meta-type without being derived
         * from a class that uses it.  This allows mixin classes that need
         * their own meta-type to work so long as their meta-type is derived
         * from this meta-type.  This condition is indicated by the pointer to
         * the generated type structure being NULL.
         */
        if (base != NULL && PyObject_TypeCheck((PyObject *)base, sms->wrapper_type_type))
        {
            self->wt_td = ((sipWrapperType *)base)->wt_td;

            if (self->wt_td != NULL)
            {
                sipEventHandler *eh;

                /* Invoke any event handlers. */
                for (eh = sms->event_handlers[sipEventPySubclassCreated]; eh != NULL; eh = eh->next)
                {
                    if (sipTypeIsClass(eh->td) && sip_is_subtype((const sipClassTypeDef *)self->wt_td, (const sipClassTypeDef *)eh->td))
                    {
                        sipPySubclassCreatedEventHandler handler = (sipPySubclassCreatedEventHandler)eh->handler;

                        if (handler(self->wt_td, self) < 0)
                            return -1;
                    }
                }
            }
        }
    }
#endif

    return 0;
}
#endif


#if 0
/*
 * The metatype setattro slot.
 */
static int WrapperType_setattro(PyObject *self, PyObject *name,
        PyObject *value)
{
#if 0
    sipSipModuleState *sms = sip_get_sip_module_state_from_wrapper_type(
            (PyTypeObject *)self);

    if (sip_container_add_lazy_attrs(wms, ((sipWrapperType *)self)->wt_td) < 0)
        return -1;
#endif

    return PyType_Type.tp_setattro(self, name, value);
}


/*
 * The metatype traverse slot.
 */
static int WrapperType_traverse(PyObject *self, visitproc visit, void *arg)
{
    Py_VISIT(Py_TYPE(self));

    return 0;
}
#endif


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
