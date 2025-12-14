/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file implements the API for the voidptr type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#include <Python.h>

#include <stddef.h>
#include <string.h>

#include "sip_voidptr.h"

#include "sip_array.h"
#include "sip_module.h"


/* The void pointer data. */
typedef struct {
    void *voidptr;
    Py_ssize_t size;
    int rw;
} VoidPtrData;


/* Forward declarations of methods. */
static PyObject *VoidPtr_asarray(PyObject *self, PyObject *args, PyObject *kw);
static PyObject *VoidPtr_ascapsule(PyObject *self, PyObject *arg);
static PyObject *VoidPtr_asstring(PyObject *self, PyObject *args,
        PyObject *kw);
static PyObject *VoidPtr_getsize(PyObject *self, PyObject *arg);
static PyObject *VoidPtr_setsize(PyObject *self, PyObject *arg);
static PyObject *VoidPtr_getwriteable(PyObject *self, PyObject *arg);
static PyObject *VoidPtr_setwriteable(PyObject *self, PyObject *arg);


/* Forward declarations of slots. */
static int VoidPtr_ass_subscript(PyObject *self, PyObject *key,
        PyObject *value);
static int VoidPtr_bool(PyObject *self);
static void VoidPtr_dealloc(PyObject *self);
static int VoidPtr_getbuffer(PyObject *self, Py_buffer *view, int flags);
static PyObject *VoidPtr_int(PyObject *self);
static PyObject *VoidPtr_item(PyObject *self, Py_ssize_t idx);
static Py_ssize_t VoidPtr_length(PyObject *self);
static PyObject *VoidPtr_new(PyTypeObject *cls, PyObject *args, PyObject *kw);
static PyObject *VoidPtr_subscript(PyObject *self, PyObject *key);
static int VoidPtr_traverse(PyObject *self, visitproc visit, void *arg);


/*
 * The type specification.  Note that we don't use the
 * METH_METHOD|METH_FASTCALL|METH_KEYWORDS calling convention to get the
 * defining class because there is no public argument parsing support.
 */
static PyMethodDef VoidPtr_Methods[] = {
    {"asarray", (PyCFunction)VoidPtr_asarray, METH_VARARGS|METH_KEYWORDS, NULL},
    {"ascapsule", (PyCFunction)VoidPtr_ascapsule, METH_NOARGS, NULL},
    {"asstring", (PyCFunction)VoidPtr_asstring, METH_VARARGS|METH_KEYWORDS, NULL},
    {"getsize", (PyCFunction)VoidPtr_getsize, METH_NOARGS, NULL},
    {"setsize", (PyCFunction)VoidPtr_setsize, METH_O, NULL},
    {"getwriteable", (PyCFunction)VoidPtr_getwriteable, METH_NOARGS, NULL},
    {"setwriteable", (PyCFunction)VoidPtr_setwriteable, METH_O, NULL},
    {NULL, NULL, 0, NULL}
};

static PyType_Slot VoidPtr_slots[] = {
    {Py_bf_getbuffer, VoidPtr_getbuffer},
    {Py_mp_ass_subscript, VoidPtr_ass_subscript},
    {Py_mp_length, VoidPtr_length},
    {Py_mp_subscript, VoidPtr_subscript},
    {Py_nb_bool, VoidPtr_bool},
    {Py_nb_int, VoidPtr_int},
    {Py_sq_item, VoidPtr_item},
    {Py_sq_length, VoidPtr_length},
    {Py_tp_dealloc, VoidPtr_dealloc},
    {Py_tp_methods, VoidPtr_Methods},
    {Py_tp_new, VoidPtr_new},
    {Py_tp_traverse, VoidPtr_traverse},
    {0, NULL}
};

static PyType_Spec VoidPtr_TypeSpec = {
    .name = _SIP_MODULE_FQ_NAME ".voidptr",
    .basicsize = -(int)sizeof (VoidPtrData),
    .flags = Py_TPFLAGS_DEFAULT |
             Py_TPFLAGS_DISALLOW_INSTANTIATION |
             Py_TPFLAGS_IMMUTABLETYPE |
             Py_TPFLAGS_HAVE_GC,
    .slots = VoidPtr_slots,
};


/* Forward declarations. */
static int check_size(VoidPtrData *vp_data);
static int check_rw(VoidPtrData *vp_data);
static int check_index(VoidPtrData *vp_data, Py_ssize_t idx);
static void bad_key(PyObject *key);
static int check_slice_size(Py_ssize_t size, Py_ssize_t value_size);
static PyObject *create_voidptr(sipSipModuleState *sms, void *voidptr,
        Py_ssize_t size, int rw);
static int vp_convertor(PyObject *arg, VoidPtrData *vp_data);
static Py_ssize_t get_size_from_arg(VoidPtrData *vp_data, Py_ssize_t size);
static VoidPtrData *get_vp_data(PyObject *self, sipSipModuleState *sms);


/*
 * Implement ascapsule() for the type.
 */
static PyObject *VoidPtr_ascapsule(PyObject *self, PyObject *arg)
{
    VoidPtrData *vp_data = get_vp_data(self, NULL);
    if (vp_data == NULL)
        return NULL;

    (void)arg;

    return PyCapsule_New(vp_data->voidptr, NULL, NULL);
}


/*
 * Implement asarray() for the type.
 */
static PyObject *VoidPtr_asarray(PyObject *self, PyObject *args, PyObject *kw)
{
    sipSipModuleState *sms = sip_get_sip_module_state(Py_TYPE(self));
    if (sms == NULL)
        return NULL;

    VoidPtrData *vp_data = get_vp_data(self, sms);
    if (vp_data == NULL)
        return NULL;

    static char *const kwlist[] = {"size", NULL};

    Py_ssize_t size = -1;

    if (!PyArg_ParseTupleAndKeywords(args, kw, "|n:asarray", kwlist, &size))
        return NULL;

    if ((size = get_size_from_arg(vp_data, size)) < 0)
        return NULL;

    return sip_array_from_bytes(sms, vp_data->voidptr, size, vp_data->rw);
}


/*
 * Implement asstring() for the type.
 */
static PyObject *VoidPtr_asstring(PyObject *self, PyObject *args, PyObject *kw)
{
    VoidPtrData *vp_data = get_vp_data(self, NULL);
    if (vp_data == NULL)
        return NULL;

    static char *const kwlist[] = {"size", NULL};

    Py_ssize_t size = -1;

    if (!PyArg_ParseTupleAndKeywords(args, kw, "|n:asstring", kwlist, &size))
        return NULL;

    if ((size = get_size_from_arg(vp_data, size)) < 0)
        return NULL;

    return PyBytes_FromStringAndSize(vp_data->voidptr, size);
}


/*
 * Implement getsize() for the type.
 */
static PyObject *VoidPtr_getsize(PyObject *self, PyObject *arg)
{
    VoidPtrData *vp_data = get_vp_data(self, NULL);
    if (vp_data == NULL)
        return NULL;

    (void)arg;

    return PyLong_FromSsize_t(vp_data->size);
}


/*
 * Implement setsize() for the type.
 */
static PyObject *VoidPtr_setsize(PyObject *self, PyObject *arg)
{
    VoidPtrData *vp_data = get_vp_data(self, NULL);
    if (vp_data == NULL)
        return NULL;

    Py_ssize_t size = PyLong_AsSsize_t(arg);

    if (PyErr_Occurred())
        return NULL;

    vp_data->size = size;

    Py_RETURN_NONE;
}


/*
 * Implement getwriteable() for the type.
 */
static PyObject *VoidPtr_getwriteable(PyObject *self, PyObject *arg)
{
    VoidPtrData *vp_data = get_vp_data(self, NULL);
    if (vp_data == NULL)
        return NULL;

    (void)arg;

    return PyBool_FromLong(vp_data->rw);
}


/*
 * Implement setwriteable() for the type.
 */
static PyObject *VoidPtr_setwriteable(PyObject *self, PyObject *arg)
{
    VoidPtrData *vp_data = get_vp_data(self, NULL);
    if (vp_data == NULL)
        return NULL;

    int rw;

    if ((rw = PyObject_IsTrue(arg)) < 0)
        return NULL;

    vp_data->rw = rw;

    Py_RETURN_NONE;
}


/*
 * Implement bool() for the type.
 */
static int VoidPtr_bool(PyObject *self)
{
    VoidPtrData *vp_data = get_vp_data(self, NULL);
    if (vp_data == NULL)
        return -1;

    return vp_data->voidptr != NULL;
}


/*
 * Implement int() for the type.
 */
static PyObject *VoidPtr_int(PyObject *self)
{
    VoidPtrData *vp_data = get_vp_data(self, NULL);
    if (vp_data == NULL)
        return NULL;

    return PyLong_FromVoidPtr(vp_data->voidptr);
}


/*
 * Implement len() for the type.
 */
static Py_ssize_t VoidPtr_length(PyObject *self)
{
    VoidPtrData *vp_data = get_vp_data(self, NULL);
    if (vp_data == NULL)
        return -1;

    if (check_size(vp_data) < 0)
        return -1;

    return vp_data->size;
}


/*
 * Implement sequence item sub-script for the type.
 */
static PyObject *VoidPtr_item(PyObject *self, Py_ssize_t idx)
{
    VoidPtrData *vp_data = get_vp_data(self, NULL);
    if (vp_data == NULL)
        return NULL;

    if (check_size(vp_data) < 0 || check_index(vp_data, idx) < 0)
        return NULL;

    return PyBytes_FromStringAndSize((char *)(vp_data->voidptr) + idx, 1);
}


/*
 * Implement mapping sub-script for the type.
 */
static PyObject *VoidPtr_subscript(PyObject *self, PyObject *key)
{
    sipSipModuleState *sms = sip_get_sip_module_state(Py_TYPE(self));
    if (sms == NULL)
        return NULL;

    VoidPtrData *vp_data = get_vp_data(self, sms);
    if (vp_data == NULL)
        return NULL;

    if (check_size(vp_data) < 0)
        return NULL;

    if (PyIndex_Check(key))
    {
        Py_ssize_t idx = PyNumber_AsSsize_t(key, PyExc_IndexError);

        if (idx == -1 && PyErr_Occurred())
            return NULL;

        if (idx < 0)
            idx += vp_data->size;

        return VoidPtr_item(self, idx);
    }

    if (PySlice_Check(key))
    {
        Py_ssize_t start, stop, step, slicelength;

        if (sip_api_convert_from_slice_object(key, vp_data->size, &start, &stop, &step, &slicelength) < 0)
            return NULL;

        if (step != 1)
        {
            PyErr_SetNone(PyExc_NotImplementedError);
            return NULL;
        }

        return create_voidptr(sms, (char *)vp_data->voidptr + start,
                slicelength, vp_data->rw);
    }

    bad_key(key);

    return NULL;
}


/*
 * Implement mapping assignment sub-script for the type.
 */
static int VoidPtr_ass_subscript(PyObject *self, PyObject *key,
        PyObject *value)
{
    VoidPtrData *vp_data = get_vp_data(self, NULL);
    if (vp_data == NULL)
        return -1;

    Py_ssize_t start, size;
    Py_buffer value_view;

    if (check_rw(vp_data) < 0 || check_size(vp_data) < 0)
        return -1;

    if (PyIndex_Check(key))
    {
        start = PyNumber_AsSsize_t(key, PyExc_IndexError);

        if (start == -1 && PyErr_Occurred())
            return -1;

        if (start < 0)
            start += vp_data->size;

        if (check_index(vp_data, start) < 0)
            return -1;

        size = 1;
    }
    else if (PySlice_Check(key))
    {
        Py_ssize_t stop, step;

        if (sip_api_convert_from_slice_object(key, vp_data->size, &start, &stop, &step, &size) < 0)
            return -1;

        if (step != 1)
        {
            PyErr_SetNone(PyExc_NotImplementedError);
            return -1;
        }
    }
    else
    {
        bad_key(key);

        return -1;
    }

    if (PyObject_GetBuffer(value, &value_view, PyBUF_CONTIG_RO) < 0)
        return -1;

    /* We could allow any item size... */
    if (value_view.itemsize != 1)
    {
        PyErr_Format(PyExc_TypeError, "'%s' must have an item size of 1",
                Py_TYPE(value_view.obj)->tp_name);

        PyBuffer_Release(&value_view);
        return -1;
    }

    if (check_slice_size(size, value_view.len) < 0)
    {
        PyBuffer_Release(&value_view);
        return -1;
    }

    memmove((char *)vp_data->voidptr + start, value_view.buf, size);

    PyBuffer_Release(&value_view);

    return 0;
}


/*
 * The buffer implementation.
 */
static int VoidPtr_getbuffer(PyObject *self, Py_buffer *buf, int flags)
{
    VoidPtrData *vp_data = get_vp_data(self, NULL);
    if (vp_data == NULL)
        return -1;

    if (check_size(vp_data) < 0)
        return -1;

    return PyBuffer_FillInfo(buf, self, vp_data->voidptr, vp_data->size,
            !vp_data->rw, flags);
}


/*
 * Implement __new__ for the type.
 */
static PyObject *VoidPtr_new(PyTypeObject *cls, PyObject *args, PyObject *kw)
{
    sipSipModuleState *sms = sip_get_sip_module_state(cls);
    if (sms == NULL)
        return NULL;

    static char *const kwlist[] = {"address", "size", "writeable", NULL};

    VoidPtrData vp_config;
    Py_ssize_t size = -1;
    int rw = -1;

    if (!PyArg_ParseTupleAndKeywords(args, kw, "O&|ni:voidptr", kwlist, vp_convertor, &vp_config, &size, &rw))
        return NULL;

    /* Use the explicit size if one was given. */
    if (size >= 0)
        vp_config.size = size;

    /* Use the explicit writeable flag if one was given. */
    if (rw >= 0)
        vp_config.rw = rw;

    /* Create the instance. */
    return create_voidptr(sms, vp_config.voidptr, vp_config.size,
            vp_config.rw);
}


/*
 * The void pointer's traverse slot.
 */
static int VoidPtr_traverse(PyObject *self, visitproc visit, void *arg)
{
    Py_VISIT(Py_TYPE(self));

    return 0;
}


/*
 * The void pointer's dealloc slot.
 */
static void VoidPtr_dealloc(PyObject *self)
{
    PyObject_GC_UnTrack(self);
    PyTypeObject *type = Py_TYPE(self);
    type->tp_free(self);
    Py_DECREF(type);
}


/*
 * A convenience function to convert a C/C++ void pointer from a Python object.
 */
void *sip_api_convert_to_void_ptr(PyObject *obj)
{
    PyErr_Clear();

    if (obj == NULL)
    {
        PyErr_SetString(PyExc_TypeError,
                _SIP_MODULE_FQ_NAME ".voidptr is NULL");
        return NULL;
    }

    VoidPtrData vp_data;

    if (vp_convertor(obj, &vp_data))
        return vp_data.voidptr;

    return PyLong_AsVoidPtr(obj);
}


/*
 * Convert a C/C++ void pointer to a sip.voidptr object.
 */
PyObject *sip_api_convert_from_void_ptr(PyObject *w_mod, void *val)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            w_mod);

    return sip_convert_from_void_ptr(wms->sip_module_state, val);
}


/*
 * Implement the conversion of a C/C++ void pointer to a sip.voidptr object.
 */
PyObject *sip_convert_from_void_ptr(sipSipModuleState *sms, void *val)
{
    return create_voidptr(sms, val, -1, TRUE);
}


/*
 * Convert a C/C++ const void pointer to a sip.voidptr object.
 */
PyObject *sip_api_convert_from_const_void_ptr(PyObject *w_mod, const void *val)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            w_mod);

    return sip_convert_from_const_void_ptr(wms->sip_module_state, val);
}


/*
 * Implement the conversion of a C/C++ const void pointer to a sip.voidptr
 * object.
 */
PyObject *sip_convert_from_const_void_ptr(sipSipModuleState *sms,
        const void *val)
{
    return create_voidptr(sms, (void *)val, -1, FALSE);
}


/*
 * Convert a sized C/C++ void pointer to a sip.voidptr object.
 */
PyObject *sip_api_convert_from_void_ptr_and_size(PyObject *w_mod, void *val,
        Py_ssize_t size)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            w_mod);

    return create_voidptr(wms->sip_module_state, val, size, TRUE);
}


/*
 * Convert a sized C/C++ const void pointer to a sip.voidptr object.
 */
PyObject *sip_api_convert_from_const_void_ptr_and_size(PyObject *w_mod,
        const void *val, Py_ssize_t size)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            w_mod);

    return create_voidptr(wms->sip_module_state, (void *)val, size, FALSE);
}


/*
 * Initialise the void pointer support.
 */
int sip_void_ptr_init(PyObject *module, sipSipModuleState *sms)
{
    sms->void_ptr_type = (PyTypeObject *)PyType_FromModuleAndSpec(module,
            &VoidPtr_TypeSpec, NULL);

    if (sms->void_ptr_type == NULL)
        return -1;

    if (PyModule_AddType(module, sms->void_ptr_type) < 0)
        return -1;

    return 0;
}


/*
 * Check that a void pointer has an explicit size and raise an exception if it
 * hasn't.
 */
static int check_size(VoidPtrData *vp_data)
{
    if (vp_data->size >= 0)
        return 0;

    PyErr_SetString(PyExc_IndexError,
            _SIP_MODULE_FQ_NAME ".voidptr object has an unknown size");

    return -1;
}


/*
 * Check that a void pointer is writable.
 */
static int check_rw(VoidPtrData *vp_data)
{
    if (vp_data->rw)
        return 0;

    PyErr_SetString(PyExc_TypeError,
            "cannot modify a read-only " _SIP_MODULE_FQ_NAME ".voidptr object");

    return -1;
}


/*
 * Check that an index is valid for a void pointer.
 */
static int check_index(VoidPtrData *vp_data, Py_ssize_t idx)
{
    if (idx >= 0 && idx < vp_data->size)
        return 0;

    PyErr_SetString(PyExc_IndexError, "index out of bounds");

    return -1;
}


/*
 * Raise an exception about a bad sub-script key.
 */
static void bad_key(PyObject *key)
{
    PyErr_Format(PyExc_TypeError,
            "cannot index a " _SIP_MODULE_FQ_NAME ".voidptr object using '%s'",
            Py_TYPE(key)->tp_name);
}


/*
 * Check that the size of a value is the same as the size of the slice it is
 * replacing.
 */
static int check_slice_size(Py_ssize_t size, Py_ssize_t value_size)
{
    if (value_size == size)
        return 0;

    PyErr_SetString(PyExc_ValueError,
            "cannot modify the size of a " _SIP_MODULE_FQ_NAME ".voidptr object");

    return -1;
}


/*
 * Do the work of converting a void pointer.
 */
static PyObject *create_voidptr(sipSipModuleState *sms, void *voidptr,
        Py_ssize_t size, int rw)
{
    PyErr_Clear();

    if (voidptr == NULL)
        Py_RETURN_NONE;

    PyObject *vp = sms->void_ptr_type->tp_alloc(sms->void_ptr_type, 0);
    if (vp == NULL)
        return NULL;

    VoidPtrData *vp_data = get_vp_data(vp, sms);
    if (vp_data == NULL)
    {
        Py_DECREF(vp);
        return NULL;
    }

    vp_data->voidptr = voidptr;
    vp_data->size = size;
    vp_data->rw = rw;

    return vp;
}


/*
 * Convert a Python object to the values needed to create a voidptr.
 */
static int vp_convertor(PyObject *arg, VoidPtrData *vp_config)
{
    VoidPtrData *vp_data;
    void *ptr;
    Py_ssize_t size = -1;
    int rw = TRUE;

    if (arg == Py_None)
    {
        ptr = NULL;
    }
    else if (PyCapsule_CheckExact(arg))
    {
        ptr = PyCapsule_GetPointer(arg, NULL);
    }
    else if ((vp_data = get_vp_data(arg, NULL)) != NULL)
    {
        ptr = vp_data->voidptr;
        size = vp_data->size;
        rw = vp_data->rw;
    }
    else if (PyObject_CheckBuffer(arg))
    {
        Py_buffer view;

        if (PyObject_GetBuffer(arg, &view, PyBUF_SIMPLE) < 0)
            return 0;

        ptr = view.buf;
        size = view.len;
        rw = !view.readonly;

        PyBuffer_Release(&view);
    }
    else
    {
        PyErr_Clear();
        ptr = PyLong_AsVoidPtr(arg);

        if (PyErr_Occurred())
        {
            PyErr_SetString(PyExc_TypeError, "a single integer, Capsule, None, bytes-like object or another " _SIP_MODULE_FQ_NAME ".voidptr object is required");
            return 0;
        }
    }

    vp_config->voidptr = ptr;
    vp_config->size = size;
    vp_config->rw = rw;

    return 1;
}


/*
 * Get a size possibly supplied as an argument, otherwise get it from the
 * object.  Raise an exception if there was no size specified.
 */
static Py_ssize_t get_size_from_arg(VoidPtrData *vp_data, Py_ssize_t size)
{
    /* Use the current size if one wasn't explicitly given. */
    if (size < 0)
        size = vp_data->size;

    if (size < 0)
        PyErr_SetString(PyExc_ValueError,
                "a size must be given or the " _SIP_MODULE_FQ_NAME ".voidptr object must have a size");

    return size;
}


/*
 * Return the data for a void pointer instance.
 */
static VoidPtrData *get_vp_data(PyObject *vp, sipSipModuleState *sms)
{
    /* Get the sip module module state if necessary. */
    if (sms == NULL)
    {
        sms = sip_get_sip_module_state(Py_TYPE(vp));
        if (sms == NULL)
            return NULL;
    }

    return (VoidPtrData *)PyObject_GetTypeData(vp, sms->void_ptr_type);
}
