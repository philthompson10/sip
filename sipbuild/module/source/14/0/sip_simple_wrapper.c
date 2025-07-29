/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The implementation of the sip simple wrapper type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <stddef.h>

#include "sip_simple_wrapper.h"

#include "sip_container.h"
#include "sip_core.h"
#include "sip_module.h"
#include "sip_object_map.h"
#include "sip_parsers.h"
#include "sip_threads.h"
#include "sip_wrapper_type.h"


/* Forward declarations of slots. */
static void SimpleWrapper_dealloc(PyObject *self);
static int SimpleWrapper_init(PyObject *self, PyObject *args, PyObject *kwds);
static PyObject *SimpleWrapper_new(PyTypeObject *cls, PyObject *args,
        PyObject *kwds);

static PyObject *SimpleWrapper_get_dict(PyObject *self, void *closure);
static int SimpleWrapper_set_dict(PyObject *self, PyObject *value,
        void *closure);


/*
 * The type specification.
 */
static PyGetSetDef SimpleWrapper_getset[] = {
    {"__dict__", SimpleWrapper_get_dict, SimpleWrapper_set_dict, NULL, NULL},
    {NULL, NULL, NULL, NULL, NULL}
};

static PyMemberDef SimpleWrapper_members[] = {
    {"__dictoffset__", Py_T_PYSSIZET, offsetof(sipSimpleWrapper, dict), Py_READONLY},
    {NULL}
};

static PyType_Slot SimpleWrapper_slots[] = {
    {Py_bf_getbuffer, sipSimpleWrapper_getbuffer},
    {Py_bf_releasebuffer, sipSimpleWrapper_releasebuffer},
    {Py_tp_clear, sipSimpleWrapper_clear},
    {Py_tp_dealloc, SimpleWrapper_dealloc},
    {Py_tp_init, SimpleWrapper_init},
    {Py_tp_new, SimpleWrapper_new},
    {Py_tp_traverse, sipSimpleWrapper_traverse},
    {Py_tp_getset, SimpleWrapper_getset},
    {Py_tp_members, SimpleWrapper_members},
    {0, NULL}
};

static PyType_Spec SimpleWrapper_TypeSpec = {
    .name = _SIP_MODULE_FQ_NAME ".simplewrapper",
    .basicsize = sizeof (sipSimpleWrapper),
    .flags = Py_TPFLAGS_DEFAULT |
             Py_TPFLAGS_BASETYPE |
#if defined(Py_TPFLAGS_DISALLOW_INSTANTIATION)
             Py_TPFLAGS_DISALLOW_INSTANTIATION |
#endif
#if defined(Py_TPFLAGS_IMMUTABLETYPE)
             Py_TPFLAGS_IMMUTABLETYPE |
#endif
             Py_TPFLAGS_HAVE_GC,
    .slots = SimpleWrapper_slots,
};


/* Forward declarations. */
#if 0
Not yet needed.
static void *explicit_access_func(sipSimpleWrapper *sw, AccessFuncOp op);
static sipFinalFunc find_finalisation(const sipClassTypeDef *ctd);
static void *indirect_access_func(sipSimpleWrapper *sw, AccessFuncOp op);
#endif


/*
 * The simple wrapper clear slot.
 */
int sipSimpleWrapper_clear(PyObject *self)
{
    sipSimpleWrapper *sw = (sipSimpleWrapper *)self;

    int vret = 0;

    /* Call any handwritten clear code. */
    void *ptr;
    const sipClassTypeDef *ctd;

    if ((ptr = sip_get_ptr_type_def(sw, &ctd)) != NULL)
        if (ctd->ctd_clear != NULL)
            vret = ctd->ctd_clear(ptr);

    Py_CLEAR(sw->dict);
    Py_CLEAR(sw->extra_refs);
    Py_CLEAR(sw->user);
    Py_CLEAR(sw->mixin_main);

    return vret;
}


/*
 * The simple wrapper dealloc slot.
 */
static void SimpleWrapper_dealloc(PyObject *self)
{
    sip_forget_object((sipSimpleWrapper *)self);

    /*
     * Now that the C++ object no longer exists we can tidy up the Python
     * object.
     */
    sipSimpleWrapper_clear(self);

    PyTypeObject *type = Py_TYPE(self);
    type->tp_free(self);
    Py_DECREF(type);
}


/*
 * The simple wrapper get buffer slot.
 */
int sipSimpleWrapper_getbuffer(PyObject *self, Py_buffer *buf, int flags)
{
    void *ptr;
    const sipClassTypeDef *ctd;

    if ((ptr = sip_get_ptr_type_def((sipSimpleWrapper *)self, &ctd)) == NULL)
        return -1;

    if (sipTypeUseLimitedAPI(&ctd->ctd_base))
    {
        sipGetBufferFuncLimited getbuffer = (sipGetBufferFuncLimited)ctd->ctd_getbuffer;
        sipBufferDef bd;

        /*
         * Ensure all fields have a default value.  This means that extra
         * fields can be appended in the future that older handwritten code
         * doesn't know about.
         */
        memset(&bd, 0, sizeof(sipBufferDef));

        if (getbuffer(self, ptr, &bd) < 0)
            return -1;

        return PyBuffer_FillInfo(buf, self, bd.bd_buffer, bd.bd_length,
                bd.bd_readonly, flags);
    }

    return ctd->ctd_getbuffer(self, ptr, buf, flags);
}


/*
 * The simple wrapper init slot.
 */
static int SimpleWrapper_init(PyObject *self, PyObject *args, PyObject *kwds)
{
#if 0
    sipSipModuleState *sms = sip_get_sip_module_state_from_wrapper_type(
            Py_TYPE(self));
    sipSimpleWrapper *sw = (sipSimpleWrapper *)self;

    void *sipNew;
    int sipFlags, from_cpp = TRUE;
    sipWrapper *owner;
    sipWrapperType *wt = (sipWrapperType *)Py_TYPE(self);
    const sipTypeDef *td = wt->wt_td;
    const sipClassTypeDef *ctd = (sipClassTypeDef *)td;
    PyObject *unused = NULL;
    sipFinalFunc final_func = find_finalisation(ctd);

    /* Check for an existing C++ instance waiting to be wrapped. */
    if (sip_get_pending(sms, &sipNew, &owner, &sipFlags) < 0)
        return -1;

    if (sipNew == NULL)
    {
        PyObject *parseErr = NULL, **unused_p = NULL;

        /* See if we are interested in any unused keyword arguments. */
        if (sipTypeCallSuperInit(&ctd->ctd_base) || final_func != NULL)
            unused_p = &unused;

        /* Call the C++ ctor. */
        owner = NULL;

        // TODO Convert args and kwds to the vector call equivalents.
        sipNew = ctd->ctd_init(sw, args, kwds, unused_p, (PyObject **)&owner,
                &parseErr);

        if (sipNew != NULL)
        {
            sipFlags = SIP_DERIVED_CLASS;
        }
        else if (parseErr == NULL)
        {
            /*
             * The C++ ctor must have raised an exception which has been
             * translated to a Python exception.
             */
            return -1;
        }
        else
        {
            sipInitExtenderDef *ie = wt->wt_iextend;

            /*
             * If we have not found an appropriate overload then try any
             * extenders.
             */
            while (PyList_Check(parseErr) && ie != NULL)
            {
                sipNew = ie->ie_extender(sw, args, kwds, &unused,
                        (PyObject **)&owner, &parseErr);

                if (sipNew != NULL)
                    break;

                ie = ie->ie_next;
            }

            if (sipNew == NULL)
            {
                const char *docstring = ctd->ctd_docstring;

                /*
                 * Use the docstring for errors if it was automatically
                 * generated.
                 */
                if (docstring != NULL)
                {
                    if (*docstring == AUTO_DOCSTRING)
                        ++docstring;
                    else
                        docstring = NULL;
                }

                sip_api_no_function(parseErr, ctd->ctd_container.cod_name,
                        docstring);

                return -1;
            }

            sipFlags = 0;
        }

        if (owner == NULL)
            sipFlags |= SIP_PY_OWNED;
        else if ((PyObject *)owner == Py_None)
        {
            /* This is the hack that means that C++ owns the new instance. */
            sipFlags |= SIP_CPP_HAS_REF;
            Py_INCREF(self);
            owner = NULL;
        }

        /* The instance was created from Python. */
        from_cpp = FALSE;
    }

    /* Handler any owner if the type supports the concept. */
    if (PyObject_TypeCheck(self, sms->wrapper_type))
    {
        /*
         * The application may be doing something very unadvisable (like
         * calling __init__() for a second time), so make sure we don't already
         * have a parent.
         */
        sip_remove_from_parent((sipWrapper *)self);

        if (owner != NULL)
        {
            assert(PyObject_TypeCheck((PyObject *)owner, sms->wrapper_type));

            sip_add_to_parent((sipWrapper *)self, (sipWrapper *)owner);
        }
    }

    sw->data = sipNew;
    sw->sw_flags = sipFlags | SIP_CREATED;

    /* Set the access function. */
    if (sipIsAccessFunc(sw))
        sw->access_func = explicit_access_func;
    else if (sipIsIndirect(sw))
        sw->access_func = indirect_access_func;
    else
        sw->access_func = NULL;

    if (!sipNotInMap(sw))
        sip_om_add_object(wms, sw);

    /* If we are wrapping an instance returned from C/C++ then we are done. */
    if (from_cpp)
    {
        /*
         * Invoke any event handlers for instances that are accessed directly.
         */
        if (sw->access_func == NULL)
        {
            sipEventHandler *eh;

            for (eh = sms->event_handlers[sipEventWrappedInstance]; eh != NULL; eh = eh->next)
            {
                if (sipTypeIsClass(eh->td) && sip_is_subtype(ctd, (const sipClassTypeDef *)eh->td))
                {
                    sipWrappedInstanceEventHandler handler = (sipWrappedInstanceEventHandler)eh->handler;

                    if (handler((const sipTypeDef *)ctd, sipNew) < 0)
                        return -1;
                }
            }
        }

        return 0;
    }

    /* Call any finalisation code. */
    if (final_func != NULL)
    {
        PyObject *new_unused = NULL, **new_unused_p;

        if (unused == NULL || unused != kwds)
        {
            /*
             * There are no unused arguments or we have already created a dict
             * containing the unused sub-set, so there is no need to create
             * another.
             */
            new_unused_p = NULL;
        }
        else
        {
            /*
             * All of the keyword arguments are unused, so if some of them are
             * now going to be used then a new dict will be needed.
             */
            new_unused_p = &new_unused;
        }
            
        if (final_func(self, sipNew, unused, new_unused_p) < 0)
        {
            Py_XDECREF(unused);
            return -1;
        }

        if (new_unused != NULL)
        {
            Py_DECREF(unused);
            unused = new_unused;
        }
    }

    /* See if we should call the equivalent of super().__init__(). */
    if (sipTypeCallSuperInit(&ctd->ctd_base))
    {
        PyObject *next;

        /* Find the next type in the MRO. */
        next = sip_next_in_mro(self, (PyObject *)sms->simple_wrapper_type);

        /*
         * If the next type in the MRO is object then take a shortcut by not
         * calling super().__init__() but emulating object.__init__() instead.
         * This will be the most common case and also allows us to generate a
         * better exception message if there are unused keyword arguments.  The
         * disadvantage is that the exception message will be different if
         * there is a mixin.
         */
        if (next != (PyObject *)&PyBaseObject_Type)
        {
            int rc = sip_super_init(self, sms->empty_tuple, unused, next);

            Py_XDECREF(unused);

            return rc;
        }
    }

    if (sms->unused_backdoor != NULL)
    {
        /*
         * We are being called by a mixin's __init__ so save any unused
         * arguments for it to pass on to the main class's __init__.
         */
        *sms->unused_backdoor = unused;
    }
    else if (unused != NULL)
    {
        /* We shouldn't have any unused keyword arguments. */
        if (PyDict_Size(unused) != 0)
        {
            PyObject *key, *value;
            Py_ssize_t pos = 0;

            /* Just report one of the unused arguments. */
            PyDict_Next(unused, &pos, &key, &value);

            PyErr_Format(PyExc_TypeError,
                    "'%S' is an unknown keyword argument", key);

            Py_DECREF(unused);

            return -1;
        }

        Py_DECREF(unused);
    }

    return 0;
#else
    return -1;
#endif
}


/*
 * The simple wrapper new slot.
 */
static PyObject *SimpleWrapper_new(PyTypeObject *cls, PyObject *args,
        PyObject *kwds)
{
#if 0
    sipSipModuleState *sms = sip_get_sip_module_state_from_wrapper_type(cls);
    sipWrapperType *wt = (sipWrapperType *)cls;
    const sipTypeDef *td = wt->wt_td;

    (void)args;
    (void)kwds;

    /* Check the base types are not being used directly. */
    // TODO Is this still necessary with the current TP_FLAGS?
    if (cls == sms->simple_wrapper_type || cls == sms->wrapper_type)
    {
        PyErr_Format(PyExc_TypeError,
                "the %s type cannot be instantiated or sub-classed",
                cls->tp_name);

        return NULL;
    }

    if (sip_container_add_lazy_attrs(wms, td) < 0)
        return NULL;

    /* See if it is a mapped type. */
    if (sipTypeIsMapped(td))
    {
        PyErr_Format(PyExc_TypeError,
                "%s.%s represents a mapped type and cannot be instantiated",
                sipNameOfModule(td->td_module),
                sip_get_container(td)->cod_name, td));

        return NULL;
    }

    /* See if it is a namespace. */
    if (sipTypeIsNamespace(td))
    {
        PyErr_Format(PyExc_TypeError,
                "%s.%s represents a C++ namespace and cannot be instantiated",
                sipNameOfModule(td->td_module),
                sip_get_container(td)->cod_name, td));

        return NULL;
    }

    /*
     * See if the object is being created explicitly rather than being wrapped.
     */
    if (!sip_is_pending(sms))
    {
        /*
         * See if it cannot be instantiated or sub-classed from Python, eg.
         * it's an opaque class.  Some restrictions might be overcome with
         * better SIP support.
         */
        if (((sipClassTypeDef *)td)->ctd_init == NULL)
        {
            PyErr_Format(PyExc_TypeError,
                    "%s.%s cannot be instantiated or sub-classed",
                    sipNameOfModule(td->td_module),
                    sip_get_container(td)->cod_name, td));

            return NULL;
        }

        /* See if it is an abstract type. */
        if (sipTypeIsAbstract(td) && !wt->wt_user_type && ((const sipClassTypeDef *)td)->ctd_init_mixin == NULL)
        {
            PyErr_Format(PyExc_TypeError,
                    "%s.%s represents a C++ abstract class and cannot be instantiated",
                    sipNameOfModule(td->td_module),
                    sip_get_container(td)->cod_name, td));

            return NULL;
        }
    }

    /* Call the standard super-type new. */
    return PyBaseObject_Type.tp_new(cls, sms->empty_tuple, NULL);
#else
    return NULL;
#endif
}


/*
 * The simple wrapper release buffer slot.
 */
void sipSimpleWrapper_releasebuffer(PyObject *self, Py_buffer *buf)
{
    void *ptr;
    const sipClassTypeDef *ctd;

    if ((ptr = sip_get_ptr_type_def((sipSimpleWrapper *)self, &ctd)) == NULL)
        return;

    if (sipTypeUseLimitedAPI(&ctd->ctd_base))
    {
        sipReleaseBufferFuncLimited releasebuffer = (sipReleaseBufferFuncLimited)ctd->ctd_releasebuffer;

        releasebuffer(self, ptr);

        return;
    }

    ctd->ctd_releasebuffer(self, ptr, buf);
}


/*
 * The simple wrapper traverse slot.
 */
int sipSimpleWrapper_traverse(PyObject *self, visitproc visit, void *arg)
{
    sipSimpleWrapper *sw = (sipSimpleWrapper *)self;

    Py_VISIT(Py_TYPE(self));

    /* Call any handwritten traverse code. */
    void *ptr;
    const sipClassTypeDef *ctd;

    if ((ptr = sip_get_ptr_type_def(sw, &ctd)) != NULL)
        if (ctd->ctd_traverse != NULL)
        {
            int vret;

            if ((vret = ctd->ctd_traverse(ptr, visit, arg)) != 0)
                return vret;
        }

    Py_VISIT(sw->dict);
    Py_VISIT(sw->extra_refs);
    Py_VISIT(sw->user);
    Py_VISIT(sw->mixin_main);

    return 0;
}


/*
 * The __dict__ getter.
 */
static PyObject *SimpleWrapper_get_dict(PyObject *self, void *closure)
{
    sipSimpleWrapper *sw = (sipSimpleWrapper *)self;

    (void)closure;

    /* Create the dictionary if needed. */
    if (sw->dict == NULL)
    {
        sw->dict = PyDict_New();

        if (sw->dict == NULL)
            return NULL;
    }

    Py_INCREF(sw->dict);
    return sw->dict;
}


/*
 * The __dict__ setter.
 */
static int SimpleWrapper_set_dict(PyObject *self, PyObject *value,
        void *closure)
{
    sipSimpleWrapper *sw = (sipSimpleWrapper *)self;

    (void)closure;

    /* Check that any new value really is a dictionary. */
    if (value != NULL && !PyDict_Check(value))
    {
        PyErr_Format(PyExc_TypeError,
                "__dict__ must be set to a dictionary, not a '%s'",
                Py_TYPE(value)->tp_name);
        return -1;
    }

    Py_XDECREF(sw->dict);
    
    Py_XINCREF(value);
    sw->dict = value;

    return 0;
}


/*
 * Initialise the simple wrapper type.
 */
int sip_simple_wrapper_init(PyObject *module, sipSipModuleState *sms)
{
#if PY_VERSION_HEX >= 0x030c0000
    sms->simple_wrapper_type = (PyTypeObject *)PyType_FromMetaclass(
            sms->wrapper_type_type, module, &SimpleWrapper_TypeSpec, NULL);
#else
    // TODO support for version prior to v3.12.
#endif

    if (sms->simple_wrapper_type == NULL)
        return -1;

    if (PyModule_AddType(module, sms->simple_wrapper_type) < 0)
        return -1;

    return 0;
}


#if 0
Not yet needed.
/*
 * The access function for handwritten access functions.
 */
static void *explicit_access_func(sipSimpleWrapper *sw, AccessFuncOp op)
{
    typedef void *(*explicitAccessFunc)(void);

    if (op == ReleaseGuard)
        return NULL;

    return ((explicitAccessFunc)(sw->data))();
}


/*
 * Find any finalisation function for a class, searching its super-classes if
 * necessary.
 */
static sipFinalFunc find_finalisation(const sipClassTypeDef *ctd)
{
    if (ctd->ctd_final != NULL)
        return ctd->ctd_final;

    const sipTypeID *supers;

    if ((supers = ctd->ctd_supers) != NULL)
    {
        sipTypeID type_id;

        do
        {
            type_id = *supers++;

            const sipClassTypeDef *sup_ctd = sip_get_generated_class_type_def(
                    type_id, ctd);
            sipFinalFunc func;

            if ((func = find_finalisation(sup_ctd)) != NULL)
                return func;
        }
        while (!sipTypeIDIsSentinel(type_id));
    }

    return NULL;
}


/*
 * The access function for indirect access.
 */
static void *indirect_access_func(sipSimpleWrapper *sw, AccessFuncOp op)
{
    void *addr;

    switch (op)
    {
    case UnguardedPointer:
        addr = sw->data;
        break;

    case GuardedPointer:
        addr = *((void **)sw->data);
        break;

    default:
        addr = NULL;
    }

    return addr;
}
#endif
