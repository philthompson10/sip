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
#include "sip_core.h"
#include "sip_enum.h"
#include "sip_module.h"
#include "sip_object_map.h"
#include "sip_parsers.h"
#include "sip_simple_wrapper.h"
#include "sip_wrapper.h"
#include "sip_wrapper_type.h"


/* Forward declarations of method implementations. */
static PyObject *meth_assign(PyObject *mod, PyObject *args);
static PyObject *meth_delete(PyObject *mod, PyObject *arg);
static PyObject *meth_dump(PyObject *mod, PyObject *arg);
static PyObject *meth_enableautoconversion(PyObject *mod, PyObject *args);
static PyObject *meth_isdeleted(PyObject *mod, PyObject *args);
static PyObject *meth_ispycreated(PyObject *mod, PyObject *args);
static PyObject *meth_ispyowned(PyObject *mod, PyObject *args);
static PyObject *meth_setdeleted(PyObject *mod, PyObject *args);
static PyObject *meth_settracemask(PyObject *mod, PyObject *args);
static PyObject *meth_transferback(PyObject *mod, PyObject *args);
static PyObject *meth_transferto(PyObject *mod, PyObject *args);
static PyObject *meth_wrapinstance(PyObject *mod, PyObject *args);
static PyObject *meth_unwrapinstance(PyObject *mod, PyObject *args);


PyMethodDef sipModuleMethods[] = {
    {"assign", meth_assign, METH_VARARGS, NULL},
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
    {"_unpickle_type", sip_unpickle_type, METH_VARARGS, NULL},
#if defined(SIP_CONFIGURATION_CustomEnums)
    {"_unpickle_enum", sip_enum_unpickle_custom_enum, METH_VARARGS, NULL},
#endif
    {NULL, NULL, 0, NULL}
};


/* Forward declarations. */
static void clear_wrapper(sipSipModuleState *sms, sipSimpleWrapper *sw);
static void print_object(const char *label, PyObject *obj);


/*
 * Invoke the assignment operator for a C++ instance.
 */
static PyObject *meth_assign(PyObject *mod, PyObject *args)
{
#if 0
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(mod);
    sipSimpleWrapper *dst, *src;

    if (!PyArg_ParseTuple(args, "O!O!:assign", sms->simple_wrapper_type, &dst, sms->simple_wrapper_type, &src))
        return NULL;

    /* Get the assignment helper. */
    sipAssignFunc assign_helper;
    PyTypeObject *dst_type = Py_TYPE(dst);
    const sipTypeDef *td = ((sipWrapperType *)dst_type)->wt_td;

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
    const sipTypeDef *super_td;
    PyTypeObject *src_type = Py_TYPE(src);

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
    void *dst_addr, *src_addr;

    if ((dst_addr = sip_api_get_cpp_ptr(dst, NULL)) == NULL)
        return NULL;

    if ((src_addr = sip_api_get_cpp_ptr(src, super_td)) == NULL)
        return NULL;

    /* Do the assignment. */
    assign_helper(dst_addr, 0, src_addr);
#endif

    Py_INCREF(Py_None);
    return Py_None;
}


/*
 * Call an instance's dtor.
 */
static PyObject *meth_delete(PyObject *mod, PyObject *args)
{
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(mod);
    sipSimpleWrapper *sw;

    if (!PyArg_ParseTuple(args, "O!:delete", sms->simple_wrapper_type, &sw))
        return NULL;

    if (sip_check_pointer(sw->data, sw) < 0)
        return NULL;

    clear_wrapper(sms, sw);

    sip_release(sw->data, (const sipTypeDef *)sw->ctd, sw->flags, NULL);

    Py_INCREF(Py_None);
    return Py_None;
}


/*
 * Dump various bits of potentially useful information to stdout.  Note that we
 * use the same calling convention as sys.getrefcount() so that it has the
 * same caveat regarding the reference count.
 */
static PyObject *meth_dump(PyObject *mod, PyObject *arg)
{
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(mod);

    if (!PyObject_TypeCheck(arg, sms->simple_wrapper_type))
    {
        PyErr_Format(PyExc_TypeError,
                "dump() argument 1 must be " _SIP_MODULE_FQ_NAME ".simplewrapper, not %s",
                Py_TYPE(arg)->tp_name);
        return NULL;
    }

    sipSimpleWrapper *sw = (sipSimpleWrapper *)arg;

    print_object(NULL, (PyObject *)sw);

    printf("    Reference count: %" PY_FORMAT_SIZE_T "d\n", Py_REFCNT(sw));
    printf("    Address of wrapped object: %p\n", sip_api_get_address(sw));
    printf("    Created by: %s\n", (sipIsDerived(sw) ? "Python" : "C/C++"));
    printf("    To be destroyed by: %s\n", (sipIsPyOwned(sw) ? "Python" : "C/C++"));

    if (PyObject_TypeCheck((PyObject *)sw, sms->wrapper_type))
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
 * Enable or disable auto-conversion of a wrapped type.
 */
static PyObject *meth_enableautoconversion(PyObject *mod, PyObject *args)
{
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(mod);
    sipWrapperType *wt;
    int enable;

    if (!PyArg_ParseTuple(args, "O!i:enableautoconversion", sms->wrapper_type_type, &wt, &enable))
        return NULL;

    int was_enabled = sip_api_enable_autoconversion(wt, enable);

    PyObject *res = (was_enabled ? Py_True : Py_False);

    Py_INCREF(res);
    return res;
}


/*
 * Check if an instance still exists without raising an exception.
 */
static PyObject *meth_isdeleted(PyObject *mod, PyObject *args)
{
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(mod);
    sipSimpleWrapper *sw;

    if (!PyArg_ParseTuple(args, "O!:isdeleted", sms->simple_wrapper_type, &sw))
        return NULL;

    PyObject *res = (sip_api_get_address(sw) == NULL ? Py_True : Py_False);

    Py_INCREF(res);
    return res;
}


/*
 * Check if an instance was created by Python.
 */
static PyObject *meth_ispycreated(PyObject *mod, PyObject *args)
{
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(mod);
    sipSimpleWrapper *sw;

    if (!PyArg_ParseTuple(args, "O!:ispycreated", sms->simple_wrapper_type, &sw))
        return NULL;

    /* sipIsDerived() is a misnomer. */
    // TODO Rename it.
    PyObject *res = (sipIsDerived(sw) ? Py_True : Py_False);

    Py_INCREF(res);
    return res;
}


/*
 * Check if an instance is owned by Python or C/C++.
 */
static PyObject *meth_ispyowned(PyObject *mod, PyObject *args)
{
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(mod);
    sipSimpleWrapper *sw;

    if (!PyArg_ParseTuple(args, "O!:ispyowned", sms->simple_wrapper_type, &sw))
        return NULL;

    PyObject *res = (sipIsPyOwned(sw) ? Py_True : Py_False);

    Py_INCREF(res);
    return res;
}


/*
 * Mark an instance as having been deleted.
 */
static PyObject *meth_setdeleted(PyObject *mod, PyObject *args)
{
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(mod);
    sipSimpleWrapper *sw;

    if (!PyArg_ParseTuple(args, "O!:setdeleted", sms->simple_wrapper_type, &sw))
        return NULL;

    clear_wrapper(sms, sw);

    Py_INCREF(Py_None);
    return Py_None;
}


/*
 * Set the trace mask.
 */
static PyObject *meth_settracemask(PyObject *mod, PyObject *args)
{
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(mod);
    unsigned new_mask;

    if (!PyArg_ParseTuple(args, "I:settracemask", &new_mask))
        return NULL;

    sms->trace_mask = new_mask;

    Py_INCREF(Py_None);
    return Py_None;
}


/*
 * Transfer the ownership of an instance to Python.
 */
static PyObject *meth_transferback(PyObject *mod, PyObject *args)
{
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(mod);
    PyObject *w;

    if (!PyArg_ParseTuple(args, "O!:transferback", sms->wrapper_type, &w))
        return NULL;

    sip_transfer_back(sms, w);

    Py_INCREF(Py_None);
    return Py_None;
}


/*
 * Transfer the ownership of an instance to C/C++.
 */
static PyObject *meth_transferto(PyObject *mod, PyObject *args)
{
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(mod);
    PyObject *w, *owner;

    if (!PyArg_ParseTuple(args, "O!O:transferto", sms->wrapper_type, &w, &owner))
        return NULL;

    if (owner == Py_None)
    {
        /*
         * Note that the Python API is different to the C API when the owner is
         * None.
         */
        owner = NULL;
    }
    else if (!PyObject_TypeCheck(owner, sms->wrapper_type))
    {
        PyErr_Format(PyExc_TypeError,
                "transferto() argument 2 must be " _SIP_MODULE_FQ_NAME ".wrapper, not %s",
                Py_TYPE(owner)->tp_name);
        return NULL;
    }

    sip_transfer_to(sms, w, owner);

    Py_INCREF(Py_None);
    return Py_None;
}


/*
 * Unwrap an instance.
 */
static PyObject *meth_unwrapinstance(PyObject *mod, PyObject *args)
{
#if 0
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(mod);
    sipSimpleWrapper *sw;

    if (!PyArg_ParseTuple(args, "O!:unwrapinstance", sms->simple_wrapper_type, &sw))
        return NULL;

    /*
     * We just get the pointer but don't try and cast it (which isn't needed
     * and wouldn't work with the way casts are currently implemented if we are
     * unwrapping something derived from a wrapped class).
     */
    void *addr = sip_api_get_cpp_ptr(sw, NULL);

    if (addr == NULL)
        return NULL;

    return PyLong_FromVoidPtr(addr);
#else
    return NULL;
#endif
}


/*
 * Wrap an instance.
 */
static PyObject *meth_wrapinstance(PyObject *mod, PyObject *args)
{
#if 0
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(mod);
    unsigned long long addr;
    sipWrapperType *wt;

    if (!PyArg_ParseTuple(args, "KO!:wrapinstance", &addr, sms->wrapper_type_type, &wt))
        return NULL;

    return sip_convert_from_type(sms, (void *)addr, wt->wt_td, NULL);
#else
    return NULL;
#endif
}


/*
 * Clear a simple wrapper.
 */
static void clear_wrapper(sipSipModuleState *sms, sipSimpleWrapper *sw)
{
    if (PyObject_TypeCheck((PyObject *)sw, sms->wrapper_type))
        sip_remove_from_parent((sipWrapper *)sw);

    /*
     * Transfer ownership to C++ so we don't try to release it when the
     * Python object is garbage collected.
     */
    sipResetPyOwned(sw);

    sipWrapperType *wt = (sipWrapperType *)Py_TYPE(sw);
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wt->wt_dmod);

    sip_om_remove_object(wms, sw);
}


/*
 * Write a reference to a wrapper to stdout.
 */
static void print_object(const char *label, PyObject *obj)
{
    if (label != NULL)
        printf("    %s: ", label);

    if (obj != NULL)
        PyObject_Print(obj, stdout, 0);
    else
        printf("NULL");

    printf("\n");
}
