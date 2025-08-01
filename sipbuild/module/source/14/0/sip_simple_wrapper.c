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
#if 0
static PyObject *SimpleWrapper_new(PyTypeObject *cls, PyObject *args,
        PyObject *kwds);
#endif

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
#if 0
    {Py_tp_new, SimpleWrapper_new},
#endif
    {Py_tp_traverse, sipSimpleWrapper_traverse},
    {Py_tp_getset, SimpleWrapper_getset},
    {Py_tp_members, SimpleWrapper_members},
    {0, NULL}
};

static PyType_Spec SimpleWrapper_TypeSpec = {
    .name = _SIP_MODULE_FQ_NAME ".simplewrapper",
    .basicsize = sizeof (sipSimpleWrapper),
    .flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
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
    printf("!!!!!!! in sipSimpleWrapper_clear()\n");
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
    printf("!!!!!!! in sipSimpleWrapper_dealloc()\n");
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
    printf("!!!!!!! in sipSimpleWrapper_getbuffer()\n");
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
#if 0
static int SimpleWrapper_init(sipSimpleWrapper *self, PyObject *args,
        PyObject *kwds)
{
    /* Do all initialisation unrelated to the arguments. */
    self->instance_init = (vectorcallfunc)instance_init;

    /*
     * Call the actual implementation.  This will convert the arguments to the
     * format the parsers can handle.
     */
    PyObject *res = PyVectorcall_Call((PyObject *)self, args, kwds);

    if (res == NULL)
        return -1;

    /* We don't care about the result. */
    Py_DECREF(res);

    return 0;
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
}
#endif


/*
 * The simple wrapper release buffer slot.
 */
void sipSimpleWrapper_releasebuffer(PyObject *self, Py_buffer *buf)
{
    printf("!!!!!!! in sipSimpleWrapper_releasebuffer()\n");
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
    printf("!!!!!!! in sipSimpleWrapper_traverse()\n");
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
    printf("!!!!!!! in sipSimpleWrapper_get_dict()\n");
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
    printf("!!!!!!! in sipSimpleWrapper_set_dict()\n");
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
 * Perform the part of the instance initialisation that deals with the
 * arguments.
 */
int sip_api_simple_wrapper_init(PyObject *dmod, sipSimpleWrapper *self,
        PyObject *args, PyObject *kwd_args, sipInstanceInitFunc init_instance,
        const sipClassTypeDef *ctd)
{
    printf("!!!!! int type init\n");
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            dmod);
    sipSipModuleState *sms = wms->sip_module_state;

    void *sipNew;
    int sipFlags, from_cpp = TRUE;
    sipWrapper *owner;
    PyObject *unused = NULL;
#if 0
    sipFinalFunc final_func = find_finalisation(ctd);
#endif

    /* Check for an existing C++ instance waiting to be wrapped. */
    if (sip_get_pending(sms, &sipNew, &owner, &sipFlags) < 0)
        return -1;

    if (sipNew == NULL)
    {
        PyObject *parseErr = NULL, **unused_p = NULL;

#if 0
        /* See if we are interested in any unused keyword arguments. */
        if (sipTypeCallSuperInit(&ctd->ctd_base) || final_func != NULL)
            unused_p = &unused;
#endif

        /* Call the C++ ctor. */
        owner = NULL;

        /*
         * Convert the traditional arguments to vectorcall style.  This steals
         * its approach from the Python internals.
         */
        assert(PyTuple_Check(args));
        Py_ssize_t nr_pos_args = (args == NULL ? 0 : PyTuple_GET_SIZE(args));

        assert(PyDict_Check(kwargs));
        Py_ssize_t nr_kwd_args = (kwd_args == NULL ? 0 : PyDict_GET_SIZE(kwd_args));

        /* Minimise the memory allocations for most cases. */
#define SMALL_ARGV 8

        PyObject *small_argv[SMALL_ARGV];
        PyObject **argv;
        Py_ssize_t nr_args = nr_pos_args + nr_kwd_args;

        if (nr_args <= SMALL_ARGV)
        {
            argv = small_argv;
        }
        else
        {
            argv = sip_api_malloc(nr_args * sizeof (PyObject *));

            if (argv == NULL)
                return -1;
        }

        Py_ssize_t i = 0;

        for (i = 0; i < nr_pos_args; i++)
            argv[i] = Py_NewRef(PyTuple_GET_ITEM(args, i));

        PyObject *kw_names;
        unsigned long names_are_strings = Py_TPFLAGS_UNICODE_SUBCLASS;

        if (nr_kwd_args == 0)
        {
            kw_names = NULL;
        }
        else
        {
            if ((kw_names = PyTuple_New(nr_kwd_args)) == NULL)
                return -1;

            Py_ssize_t pos = 0;
            PyObject *key, *value;
            i = 0;

            while (PyDict_Next(kwd_args, &pos, &key, &value))
            {
                names_are_strings &= Py_TYPE(key)->tp_flags;
                PyTuple_SET_ITEM(kw_names, i, Py_NewRef(key));
                argv[nr_pos_args + i] = Py_NewRef(value);
                i++;
            }
        }

        if (names_are_strings)
            sipNew = init_instance(dmod, self, argv, nr_pos_args, kw_names,
#if 0
                    unused_p, (PyObject **)&owner, &parseErr);
#else
                    NULL, (PyObject **)&owner, &parseErr);
#endif
        else
            PyErr_SetString(PyExc_TypeError, "keywords must be strings");

        Py_XDECREF(kw_names);

        for (i = 0; i < nr_args; i++)
            Py_DECREF(argv[i]);

        if (argv != small_argv)
            sip_api_free(argv);

        if (!names_are_strings)
            return -1;

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

    self->data = sipNew;
    self->sw_flags = sipFlags | SIP_CREATED;

#if 0
    /* Set the access function. */
    if (sipIsAccessFunc(self))
        self->access_func = explicit_access_func;
    else if (sipIsIndirect(self))
        self->access_func = indirect_access_func;
    else
#endif
        self->access_func = NULL;

    if (!sipNotInMap(self))
        sip_om_add_object(wms, self, ctd);

    /* If we are wrapping an instance returned from C/C++ then we are done. */
    if (from_cpp)
    {
#if 0
        /*
         * Invoke any event handlers for instances that are accessed directly.
         */
        if (self->access_func == NULL)
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

#if 0
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
#endif

#if 0
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
#endif

    printf("!!!!! returning from type init\n");
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
