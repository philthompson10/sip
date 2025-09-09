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

#include "sip_core.h"
#include "sip_module.h"
#include "sip_object_map.h"
#include "sip_parsers.h"
#include "sip_threads.h"
#include "sip_wrapper.h"
#include "sip_wrapper_type.h"


/*
 * The type's getters and setters..
 */
static PyObject *SimpleWrapper_get_dict(PyObject *self, void *closure);
static int SimpleWrapper_set_dict(PyObject *self, PyObject *value,
        void *closure);

static PyGetSetDef SimpleWrapper_getset[] = {
    {"__dict__", SimpleWrapper_get_dict, SimpleWrapper_set_dict, NULL, NULL},
    {NULL, NULL, NULL, NULL, NULL}
};


/*
 * The type's members.
 */
static PyMemberDef SimpleWrapper_members[] = {
    {"__dictoffset__", Py_T_PYSSIZET, offsetof(sipSimpleWrapper, dict), Py_READONLY},
    {NULL}
};


/*
 * The type's slots.
 */
static int SimpleWrapper_clear(sipSimpleWrapper *self);
static void SimpleWrapper_dealloc(sipSimpleWrapper *self);
//static int SimpleWrapper_getbuffer(PyObject *self, Py_buffer *buf, int flags);
static int SimpleWrapper_init(sipSimpleWrapper *self, PyObject *args,
        PyObject *kwd_args);
//static void SimpleWrapper_releasebuffer(PyObject *self, Py_buffer *buf);
static int SimpleWrapper_traverse(sipSimpleWrapper *self, visitproc visit,
        void *arg);

static PyType_Slot SimpleWrapper_slots[] = {
    //{Py_bf_getbuffer, sipSimpleWrapper_getbuffer},
    //{Py_bf_releasebuffer, sipSimpleWrapper_releasebuffer},
    {Py_tp_clear, SimpleWrapper_clear},
    {Py_tp_dealloc, SimpleWrapper_dealloc},
    {Py_tp_getset, SimpleWrapper_getset},
    {Py_tp_init, SimpleWrapper_init},
    {Py_tp_members, SimpleWrapper_members},
    {Py_tp_traverse, SimpleWrapper_traverse},
    {0, NULL}
};


/*
 * The type specification.
 */
static PyType_Spec SimpleWrapper_TypeSpec = {
    .name = _SIP_MODULE_FQ_NAME ".simplewrapper",
    .basicsize = sizeof (sipSimpleWrapper),
    .flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_HAVE_GC,
    .slots = SimpleWrapper_slots,
};


/* Forward declarations. */
static sipFinalFunc find_finalisation(sipWrappedModuleState *wms,
        const sipClassTypeDef *ctd);


/*
 * The simple wrapper clear slot.
 */
static int SimpleWrapper_clear(sipSimpleWrapper *self)
{
    sipWrapperType *wt = (sipWrapperType *)Py_TYPE(self);
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wt->wt_dmod);
    int vret = 0;

    /*
     * Call any handwritten clear code.  Note that this can be called after the
     * the C/C++ instance has been destroyed (because we can be called by
     * sipWrapper_dealloc()).  This feels wrong but we retain this historical
     * behaviour as it doesn't seem to have caused problems in the wild.
     */
    sipClearFunc clear = ((const sipClassTypeDef *)wt->wt_td)->ctd_clear;

    if (clear != NULL)
        vret = clear(self->data);

    Py_CLEAR(self->dict);
    Py_CLEAR(self->extra_refs);
    Py_CLEAR(self->mixin_main);
    Py_CLEAR(self->user);

    /* Handle any children if the type supports the concept. */
    if (wt->wt_is_wrapper)
    {
        sipWrapper *w = (sipWrapper *)self;

        /* Detach any children (which will be owned by C/C++). */
        while (w->first_child != NULL)
            sip_remove_from_parent(w->first_child);
    }

    return vret;
}


/*
 * The simple wrapper dealloc slot.
 */
static void SimpleWrapper_dealloc(sipSimpleWrapper *self)
{
    PyObject_GC_UnTrack((PyObject *)self);

    /*
     * Remove the object from the map and call the C/C++ dtor if we own the
     * instance.
     */
    sipWrapperType *wt = (sipWrapperType *)Py_TYPE(self);
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wt->wt_dmod);
    sipSipModuleState *sms = wms->sip_module_state;

#if 0
    // TODO
    /* Invoke any event handlers. */
    sipEventHandler *eh;

    for (eh = sms->event_handlers[sipEventCollectingWrapper]; eh != NULL; eh = eh->next)
    {
        if (sipTypeIsClass(eh->td) && sip_is_subtype(wms, ctd, (const sipClassTypeDef *)eh->td))
        {
            sipCollectingWrapperEventHandler handler = (sipCollectingWrapperEventHandler)eh->handler;

            handler((const sipTypeDef *)ctd, self);
        }
    }
#endif

    /*
     * Remove the object from the map before calling the class specific dealloc
     * code.  This code calls the C++ dtor and may result in further calls that
     * pass the instance as an argument.  If this is still in the map then it's
     * reference count would be increased (to one) and bad things happen when
     * it drops back to zero again.  (An example is PyQt events generated
     * during the dtor call being passed to an event filter implemented in
     * Python.)  By removing it from the map first we ensure that a new Python
     * object is created.
     */
    sip_om_remove_object(wms, self);

    if (sms->interpreter_state != NULL)
    {
        sipDeallocFunc dealloc = ((const sipClassTypeDef *)wt->wt_td)->ctd_dealloc;

        if (dealloc != NULL)
            dealloc(self);
    }

    /*
     * Now that the C++ object no longer exists (as far as we are concerned) we
     * can tidy up the Python object.
     */
    SimpleWrapper_clear(self);

    PyTypeObject *type = Py_TYPE(self);
    type->tp_free(self);
    Py_DECREF(type);
}


/*
 * The simple wrapper get buffer slot.
 */
#if 0
// TODO
static int SimpleWrapper_getbuffer(PyObject *self, Py_buffer *buf, int flags)
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
#endif


// TODO Test that the behaviour here can be achieved with appropriate TP_FLAGS.
#if 0
/*
 * The simple wrapper new slot.
 */
static PyObject *SimpleWrapper_new(PyTypeObject *cls, PyObject *args,
        PyObject *kwds)
{
    sipSipModuleState *sms = sip_get_sip_module_state_from_sip_type(cls);
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
}
#endif


/*
 * The simple wrapper release buffer slot.
 */
#if 0
// TODO
static void SimpleWrapper_releasebuffer(PyObject *self, Py_buffer *buf)
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
#endif


/*
 * The simple wrapper traverse slot.
 */
static int SimpleWrapper_traverse(sipSimpleWrapper *self, visitproc visit,
        void *arg)
{
    sipWrapperType *wt = (sipWrapperType *)Py_TYPE(self);
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wt->wt_dmod);

    Py_VISIT(Py_TYPE(self));

    /* Call any handwritten traverse code. */
    sipTraverseFunc traverse = ((const sipClassTypeDef *)wt->wt_td)->ctd_traverse;

    if (traverse != NULL)
    {
        int vret = traverse(self->data, visit, arg);

        if (vret != 0)
            return vret;
    }

    Py_VISIT(self->dict);
    Py_VISIT(self->extra_refs);
    Py_VISIT(self->mixin_main);
    Py_VISIT(self->user);

    /* Handle any children if the type supports the concept. */
    if (wt->wt_is_wrapper)
    {
        sipWrapper *w = ((sipWrapper *)self)->first_child;

        while (w != NULL)
        {
            /*
             * We don't traverse if the wrapper is a child of itself.  We do
             * this so that wrapped objects returned by virtual methods with
             * the /Factory/ don't have those objects collected.  This then
             * means that plugins implemented in Python have a chance of
             * working.
             */
            if (w != (sipWrapper *)self)
            {
                int vret = visit((PyObject *)w, arg);

                if (vret != 0)
                    return vret;
            }

            w = w->sibling_next;
        }
    }

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
 * The simple wrapper init slot.
 */
static int SimpleWrapper_init(sipSimpleWrapper *self, PyObject *args,
        PyObject *kwargs)
{
    sipWrapperType *wt = (sipWrapperType *)Py_TYPE(self);
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wt->wt_dmod);
    sipSipModuleState *sms = wms->sip_module_state;

    void *sipNew;
    sipWrapper *owner;
    int sipFlags;

    /* Check for an existing C++ instance waiting to be wrapped. */
    if (sip_get_pending(sms, &sipNew, &owner, &sipFlags) < 0)
        return -1;

    int from_cpp = TRUE;
    PyObject *unused = NULL;
    const sipClassTypeDef *ctd = (const sipClassTypeDef *)wt->wt_td;
    sipFinalFunc final_func = find_finalisation(wms, ctd);

    if (sipNew == NULL)
    {
        PyObject *parseErr = NULL, **unused_p = NULL;

        /* See if we are interested in any unused keyword arguments. */
        if (sipTypeCallSuperInit(&ctd->ctd_base) || final_func != NULL)
            unused_p = &unused;

        /* Call the C++ ctor. */
        owner = NULL;

        /* Convert the traditional arguments to vectorcall style. */
#define SMALL_ARGV 8
        PyObject *small_argv[SMALL_ARGV];
        Py_ssize_t argv_len = SMALL_ARGV;

        PyObject **argv, *kw_names;
        Py_ssize_t nr_pos_args;

        if (sip_vectorcall_create(args, kwargs, small_argv, &argv_len, &argv, &nr_pos_args, &kw_names) < 0)
            return -1;

        sipNew = ctd->ctd_init(self, argv, nr_pos_args, kw_names, unused_p,
                (PyObject **)&owner, &parseErr);

        sip_vectorcall_dispose(small_argv, argv, argv_len, kw_names);

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
#if 0
            sipInitExtenderDef *ie = wt->wt_iextend;

            /*
             * If we have not found an appropriate overload then try any
             * extenders.
             */
            while (PyList_Check(parseErr) && ie != NULL)
            {
                sipNew = ie->ie_extender(self, args, kwd_args, &unused,
                        (PyObject **)&owner, &parseErr);

                if (sipNew != NULL)
                    break;

                ie = ie->ie_next;
            }
#endif

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
        {
            sipFlags |= SIP_PY_OWNED;
        }
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

    /* Handle any owner if the type supports the concept. */
    if (wt->wt_is_wrapper)
    {
        /*
         * The application may be doing something very unadvisable (like
         * calling __init__() for a second time), so make sure we don't already
         * have a parent.
         */
        sip_remove_from_parent((sipWrapper *)self);

        if (owner != NULL)
        {
            // TODO If owner might be bad (ie. it comes from the user and
            // hasn't already been checked) then check properly (and earlier).
            assert(PyObject_TypeCheck((PyObject *)owner, sms->wrapper_type));

            sip_add_to_parent((sipWrapper *)self, (sipWrapper *)owner);
        }
    }

    self->data = sipNew;
    self->flags = sipFlags | SIP_CREATED;

    sip_om_add_object(wms, self);

    /* If we are wrapping an instance returned from C/C++ then we are done. */
    if (from_cpp)
    {
#if 0
        /* Invoke any event handlers. */
        sipEventHandler *eh;

        for (eh = sms->event_handlers[sipEventWrappedInstance]; eh != NULL; eh = eh->next)
        {
            if (sipTypeIsClass(eh->td) && sip_is_subtype(wms, ctd, (const sipClassTypeDef *)eh->td))
            {
                sipWrappedInstanceEventHandler handler = (sipWrappedInstanceEventHandler)eh->handler;

                if (handler((const sipTypeDef *)ctd, sipNew) < 0)
                    return -1;
            }
        }
#endif

        return 0;
    }

#if 0
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
#endif

    if (unused != NULL)
    {
    printf("!!! unused: ");
    PyObject_Print(unused, stdout, 0);
    printf("\n");
    }
    /* See if we should call the equivalent of super().__init__(). */
    if (sipTypeCallSuperInit(&ctd->ctd_base))
    {
        PyObject *next;

        /* Find the next type in the MRO. */
        next = sip_next_in_mro((PyObject *)self,
                (PyObject *)sms->simple_wrapper_type);

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
            int rc = sip_super_init((PyObject *)self, sms->empty_tuple, unused,
                    next);

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
        PyObject *unused_names = PyDict_Keys(unused);
        Py_DECREF(unused);

        if (unused_names == NULL)
            return -1;

        Py_ssize_t nr_names = PyList_GET_SIZE(unused_names);

        if (nr_names == 1)
        {
            PyErr_Format(PyExc_TypeError, "%'S' is an unknown keyword argument",
                    PyList_GET_ITEM(unused_names, 0));
            Py_DECREF(unused_names);

            return -1;
        }
        else if (nr_names > 1)
        {
            PyObject *sep = PyUnicode_FromString("', '");
            if (sep == NULL)
            {
                Py_DECREF(unused_names);
                return -1;
            }

            PyObject *names = PyUnicode_Join(sep, unused_names);
            Py_DECREF(sep);
            Py_DECREF(unused_names);

            PyErr_Format(PyExc_TypeError, "'%S' are unknown keyword arguments",
                    names);
            Py_DECREF(names);

            return -1;
        }

        Py_DECREF(unused_names);
    }

    return 0;
}


/*
 * Initialise the simple wrapper type.
 */
int sip_simple_wrapper_init(PyObject *module, sipSipModuleState *sms)
{
    sms->simple_wrapper_type = (PyTypeObject *)PyType_FromMetaclass(
            sms->wrapper_type_type, module, &SimpleWrapper_TypeSpec, NULL);

    if (sms->simple_wrapper_type == NULL)
        return -1;

    if (PyModule_AddType(module, sms->simple_wrapper_type) < 0)
        return -1;

    return 0;
}


/*
 * Find any finalisation function for a class, searching its super-classes if
 * necessary.
 */
static sipFinalFunc find_finalisation(sipWrappedModuleState *wms,
        const sipClassTypeDef *ctd)
{
    if (ctd->ctd_final != NULL)
        return ctd->ctd_final;

    const sipTypeID *supers = ctd->ctd_supers;

    if (supers != NULL)
    {
        sipTypeID sup_type_id;

        do
        {
            sup_type_id = *supers++;

            sipWrappedModuleState *defining_wms;
            const sipTypeDef *sup_td = sip_get_type_def(wms, sup_type_id,
                    &defining_wms);

            if (sup_td == NULL)
                return NULL;

            sipFinalFunc func = find_finalisation(defining_wms,
                    (const sipClassTypeDef *)sup_td);

            if (func != NULL)
                return func;
        }
        while (!sipTypeIDIsSentinel(sup_type_id));
    }

    return NULL;
}
