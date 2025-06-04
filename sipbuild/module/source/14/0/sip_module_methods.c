/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The implementation of the sip module's methods.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdio.h>

#include "sip_module_methods.h"

#include "sip.h"
#include "sip_enum.h"


/* Forward declarations. */
static PyObject *meth_assign(PyObject *self, PyObject *args);
static PyObject *meth_cast(PyObject *self, PyObject *args);
static PyObject *meth_delete(PyObject *self, PyObject *arg);
static PyObject *meth_dump(PyObject *self, PyObject *arg);
static PyObject *meth_enableautoconversion(PyObject *self, PyObject *args);
static PyObject *meth_isdeleted(PyObject *self, PyObject *args);
static PyObject *meth_ispycreated(PyObject *self, PyObject *args);
static PyObject *meth_ispyowned(PyObject *self, PyObject *args);
static PyObject *meth_setdeleted(PyObject *self, PyObject *args);
static PyObject *meth_settracemask(PyObject *self, PyObject *args);
static PyObject *meth_transferback(PyObject *self, PyObject *args);
static PyObject *meth_transferto(PyObject *self, PyObject *args);
static PyObject *meth_wrapinstance(PyObject *self, PyObject *args);
static PyObject *meth_unwrapinstance(PyObject *self, PyObject *args);
static PyObject *meth__unpickle_type(PyObject *self, PyObject *args);


    // TODO METH_FASTCALL
PyMethodDef sipModuleMethods[] = {
    {"assign", meth_assign, METH_VARARGS, NULL},
    {"cast", meth_cast, METH_VARARGS, NULL},
    {"delete", meth_delete, METH_VARARGS, NULL},
    {"dump", meth_dump, METH_O, NULL},
    {"enableautoconversion", meth_enableautoconversion, METH_VARARGS, NULL},
    {"isdeleted", meth_isdeleted, METH_VARARGS, NULL},
    {"ispycreated", meth_ispycreated, METH_VARARGS, NULL},
    {"ispyowned", meth_ispyowned, METH_VARARGS, NULL},
    {"setdeleted", meth_setdeleted, METH_VARARGS, NULL},
    {"settracemask", meth_settracemask, METH_VARARGS, NULL},
    {"transferback", meth_transferback, METH_VARARGS, NULL},
    {"transferto", meth_transferto, METH_VARARGS, NULL},
    {"wrapinstance", meth_wrapinstance, METH_VARARGS, NULL},
    {"unwrapinstance", meth_unwrapinstance, METH_VARARGS, NULL},
    {"_unpickle_type", meth__unpickle_type, METH_VARARGS, NULL},
#if defined(SIP_CONFIGURATION_CustomEnums)
    {"_unpickle_enum", sip_enum_unpickle_custom_enum, METH_VARARGS, NULL},
#endif
    {NULL, NULL, 0, NULL}
};


/*
 * Invoke the assignment operator for a C++ instance.
 */
static PyObject *meth_assign(PyObject *self, PyObject *args)
{
    sipSimpleWrapper *dst, *src;
    PyTypeObject *dst_type, *src_type;
    const sipTypeDef *td, *super_td;
    sipAssignFunc assign_helper;
    void *dst_addr, *src_addr;

    (void)self;

    if (!PyArg_ParseTuple(args, "O!O!:assign", &sipSimpleWrapper_Type, &dst, &sipSimpleWrapper_Type, &src))
        return NULL;

    /* Get the assignment helper. */
    dst_type = Py_TYPE(dst);
    td = ((sipWrapperType *)dst_type)->wt_td;

    if (sipTypeIsMapped(td))
        assign_helper = ((const sipMappedTypeDef *)td)->mtd_assign;
    else
        assign_helper = ((const sipClassTypeDef *)td)->ctd_assign;

    if (assign_helper == NULL)
    {
        PyErr_SetString(PyExc_TypeError,
                "argument 1 of assign() does not support assignment");
        return NULL;
    }

    /* Check the types are compatible. */
    src_type = Py_TYPE(src);

    if (src_type == dst_type)
    {
        super_td = NULL;
    }
    else if (PyType_IsSubtype(src_type, dst_type))
    {
        super_td = td;
    }
    else
    {
        PyErr_SetString(PyExc_TypeError,
                "type of argument 1 of assign() must be a super-type of type of argument 2");
        return NULL;
    }

    /* Get the addresses. */
    if ((dst_addr = sip_api_get_cpp_ptr(dst, NULL)) == NULL)
        return NULL;

    if ((src_addr = sip_api_get_cpp_ptr(src, super_td)) == NULL)
        return NULL;

    /* Do the assignment. */
    assign_helper(dst_addr, 0, src_addr);

    Py_INCREF(Py_None);
    return Py_None;
}


/*
 * Cast an instance to one of it's sub or super-classes by returning a new
 * Python object with the superclass type wrapping the same C++ instance.
 */
static PyObject *meth_cast(PyObject *self, PyObject *args)
{
    sipSimpleWrapper *sw;
    sipWrapperType *wt;
    const sipTypeDef *td;
    void *addr;
    PyTypeObject *ft, *tt;

    (void)self;

    if (!PyArg_ParseTuple(args, "O!O!:cast", &sipSimpleWrapper_Type, &sw, &sipWrapperType_Type, &wt))
        return NULL;

    ft = Py_TYPE(sw);
    tt = (PyTypeObject *)wt;

    if (ft == tt || PyType_IsSubtype(tt, ft))
        td = NULL;
    else if (PyType_IsSubtype(ft, tt))
        td = wt->wt_td;
    else
    {
        PyErr_SetString(PyExc_TypeError, "argument 1 of cast() must be an instance of a sub or super-type of argument 2");
        return NULL;
    }

    if ((addr = sip_api_get_cpp_ptr(sw, td)) == NULL)
        return NULL;

    /*
     * We don't put this new object into the map so that the original object is
     * always found.  It would also totally confuse the map logic.
     */
    return wrap_simple_instance(addr, wt->wt_td, NULL,
            (sw->sw_flags | SIP_NOT_IN_MAP) & ~SIP_PY_OWNED);
}


/*
 * Call an instance's dtor.
 */
static PyObject *meth_delete(PyObject *self, PyObject *args)
{
    sipSimpleWrapper *sw;
    void *addr;
    const sipClassTypeDef *ctd;

    (void)self;

    if (!PyArg_ParseTuple(args, "O!:delete", &sipSimpleWrapper_Type, &sw))
        return NULL;

    addr = getPtrTypeDef(sw, &ctd);

    if (checkPointer(addr, sw) < 0)
        return NULL;

    clear_wrapper(sw);

    release(addr, (const sipTypeDef *)ctd, sw->sw_flags, NULL);

    Py_INCREF(Py_None);
    return Py_None;
}


/*
 * Dump various bits of potentially useful information to stdout.  Note that we
 * use the same calling convention as sys.getrefcount() so that it has the
 * same caveat regarding the reference count.
 */
static PyObject *meth_dump(PyObject *self, PyObject *arg)
{
    sipSimpleWrapper *sw;

    (void)self;

    if (!PyObject_TypeCheck(arg, (PyTypeObject *)&sipSimpleWrapper_Type))
    {
        PyErr_Format(PyExc_TypeError,
                "dump() argument 1 must be " _SIP_MODULE_FQ_NAME ".simplewrapper, not %s",
                Py_TYPE(arg)->tp_name);
        return NULL;
    }

    sw = (sipSimpleWrapper *)arg;

    print_object(NULL, (PyObject *)sw);

    printf("    Reference count: %" PY_FORMAT_SIZE_T "d\n", Py_REFCNT(sw));
    printf("    Address of wrapped object: %p\n", sip_api_get_address(sw));
    printf("    Created by: %s\n", (sipIsDerived(sw) ? "Python" : "C/C++"));
    printf("    To be destroyed by: %s\n", (sipIsPyOwned(sw) ? "Python" : "C/C++"));

    if (PyObject_TypeCheck((PyObject *)sw, (PyTypeObject *)&sipWrapper_Type))
    {
        sipWrapper *w = (sipWrapper *)sw;

        print_object("Parent wrapper", (PyObject *)w->parent);
        print_object("Next sibling wrapper", (PyObject *)w->sibling_next);
        print_object("Previous sibling wrapper", (PyObject *)w->sibling_prev);
        print_object("First child wrapper", (PyObject *)w->first_child);
    }

    Py_INCREF(Py_None);
    return Py_None;
}


/*
 * Enable or disable auto-conversion of a class that supports it.
 */
static PyObject *meth_enableautoconversion(PyObject *self, PyObject *args)
{
    sipWrapperType *wt;
    int enable;

    (void)self;

    if (PyArg_ParseTuple(args, "O!i:enableautoconversion", &sipWrapperType_Type, &wt, &enable))
    {
        sipTypeDef *td = wt->wt_td;
        int was_enabled;
        PyObject *res;

        if (!sipTypeIsClass(td) || ((sipClassTypeDef *)td)->ctd_cfrom == NULL)
        {
            PyErr_Format(PyExc_TypeError,
                    "%s is not a wrapped class that supports optional auto-conversion", ((PyTypeObject *)wt)->tp_name);

            return NULL;
        }

        if ((was_enabled = sip_api_enable_autoconversion(td, enable)) < 0)
            return NULL;

        res = (was_enabled ? Py_True : Py_False);

        Py_INCREF(res);
        return res;
    }

    return NULL;
}


/*
 * Check if an instance still exists without raising an exception.
 */
static PyObject *meth_isdeleted(PyObject *self, PyObject *args)
{
    sipSimpleWrapper *sw;
    PyObject *res;

    (void)self;

    if (!PyArg_ParseTuple(args, "O!:isdeleted", &sipSimpleWrapper_Type, &sw))
        return NULL;

    res = (sip_api_get_address(sw) == NULL ? Py_True : Py_False);

    Py_INCREF(res);
    return res;
}


/*
 * Check if an instance was created by Python.
 */
static PyObject *meth_ispycreated(PyObject *self, PyObject *args)
{
    sipSimpleWrapper *sw;
    PyObject *res;

    (void)self;

    if (!PyArg_ParseTuple(args, "O!:ispycreated", &sipSimpleWrapper_Type, &sw))
        return NULL;

    /* sipIsDerived() is a misnomer. */
    res = (sipIsDerived(sw) ? Py_True : Py_False);

    Py_INCREF(res);
    return res;
}


/*
 * Check if an instance is owned by Python or C/C++.
 */
static PyObject *meth_ispyowned(PyObject *self, PyObject *args)
{
    sipSimpleWrapper *sw;
    PyObject *res;

    (void)self;

    if (!PyArg_ParseTuple(args, "O!:ispyowned", &sipSimpleWrapper_Type, &sw))
        return NULL;

    res = (sipIsPyOwned(sw) ? Py_True : Py_False);

    Py_INCREF(res);
    return res;
}


/*
 * Mark an instance as having been deleted.
 */
static PyObject *meth_setdeleted(PyObject *self, PyObject *args)
{
    sipSimpleWrapper *sw;

    (void)self;

    if (!PyArg_ParseTuple(args, "O!:setdeleted", &sipSimpleWrapper_Type, &sw))
        return NULL;

    clear_wrapper(sw);

    Py_INCREF(Py_None);
    return Py_None;
}


/*
 * Set the trace mask.
 */
static PyObject *meth_settracemask(PyObject *self, PyObject *args)
{
    unsigned new_mask;

    (void)self;

    if (PyArg_ParseTuple(args, "I:settracemask", &new_mask))
    {
        traceMask = new_mask;

        Py_INCREF(Py_None);
        return Py_None;
    }

    return NULL;
}


/*
 * Transfer the ownership of an instance to Python.
 */
static PyObject *meth_transferback(PyObject *self, PyObject *args)
{
    PyObject *w;

    (void)self;

    if (PyArg_ParseTuple(args, "O!:transferback", &sipWrapper_Type, &w))
    {
        sip_api_transfer_back(w);

        Py_INCREF(Py_None);
        return Py_None;
    }

    return NULL;
}


/*
 * Transfer the ownership of an instance to C/C++.
 */
static PyObject *meth_transferto(PyObject *self, PyObject *args)
{
    PyObject *w, *owner;

    (void)self;

    if (PyArg_ParseTuple(args, "O!O:transferto", &sipWrapper_Type, &w, &owner))
    {
        if (owner == Py_None)
        {
            /*
             * Note that the Python API is different to the C API when the
             * owner is None.
             */
            owner = NULL;
        }
        else if (!PyObject_TypeCheck(owner, (PyTypeObject *)&sipWrapper_Type))
        {
            PyErr_Format(PyExc_TypeError,
                    "transferto() argument 2 must be " _SIP_MODULE_FQ_NAME ".wrapper, not %s",
                    Py_TYPE(owner)->tp_name);
            return NULL;
        }

        sip_api_transfer_to(w, owner);

        Py_INCREF(Py_None);
        return Py_None;
    }

    return NULL;
}


/*
 * Unwrap an instance.
 */
static PyObject *meth_unwrapinstance(PyObject *self, PyObject *args)
{
    sipSimpleWrapper *sw;

    (void)self;

    if (PyArg_ParseTuple(args, "O!:unwrapinstance", &sipSimpleWrapper_Type, &sw))
    {
        void *addr;

        /*
         * We just get the pointer but don't try and cast it (which isn't
         * needed and wouldn't work with the way casts are currently
         * implemented if we are unwrapping something derived from a wrapped
         * class).
         */
        if ((addr = sip_api_get_cpp_ptr(sw, NULL)) == NULL)
            return NULL;

        return PyLong_FromVoidPtr(addr);
    }

    return NULL;
}


/*
 * Wrap an instance.
 */
static PyObject *meth_wrapinstance(PyObject *self, PyObject *args)
{
    unsigned long long addr;
    sipWrapperType *wt;

    (void)self;

    if (PyArg_ParseTuple(args, "KO!:wrapinstance", &addr, &sipWrapperType_Type, &wt))
        return sip_api_convert_from_type((void *)addr, wt->wt_td, NULL);

    return NULL;
}


/*
 * The type unpickler.
 */
static PyObject *meth__unpickle_type(PyObject *obj, PyObject *args)
{
    PyObject *mname_obj, *init_args;
    const char *tname;
    sipExportedModuleDef *em;
    int i;

    (void)obj;

    if (!PyArg_ParseTuple(args, "UsO!:_unpickle_type", &mname_obj, &tname, &PyTuple_Type, &init_args))
        return NULL;

    /* Get the module definition. */
    if ((em = sip_get_module(mname_obj)) == NULL)
        return NULL;

    /* Find the class type object. */
    for (i = 0; i < em->em_nrtypes; ++i)
    {
        sipTypeDef *td = em->em_types[i];

        if (td != NULL && !sipTypeIsStub(td) && sipTypeIsClass(td))
        {
            const char *pyname = sipPyNameOfContainer(
                    &((sipClassTypeDef *)td)->ctd_container, td);

            if (strcmp(pyname, tname) == 0)
                return PyObject_CallObject((PyObject *)sipTypeAsPyTypeObject(td), init_args);
        }
    }

    PyErr_Format(PyExc_SystemError, "unable to find to find type: %s", tname);

    return NULL;
}
