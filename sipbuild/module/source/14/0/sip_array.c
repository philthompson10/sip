/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file implements the API for the array type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <stddef.h>
#include <string.h>

#include "sip_array.h"

#include "sip.h"
#include "sip_core.h"
#include "sip_int_convertors.h"
#include "sip_module.h"
#include "sip_parsers.h"
#include "sip_wrapper_type.h"


/*
 * The object data structure.
 */
typedef struct {
    PyObject_HEAD
    void *data;
    sipWrappedModuleState *wms;
    sipTypeID type_id;
    const char *format;
    size_t stride;
    Py_ssize_t len;
    int flags;
    PyObject *owner;
} Array;


/* Forward declarations of slots. */
static int Array_ass_subscript(PyObject *self, PyObject *key, PyObject *value);
static int Array_clear(PyObject *self);
static void Array_dealloc(PyObject *self);
static int Array_getbuffer(PyObject *self, Py_buffer *view, int flags);
static PyObject *Array_item(PyObject *self, Py_ssize_t idx);
static Py_ssize_t Array_length(PyObject *self);
static PyObject *Array_new(PyTypeObject *cls, PyObject *args, PyObject *kw);
static PyObject *Array_repr(PyObject *self);
static PyObject *Array_subscript(PyObject *self, PyObject *key);
static int Array_traverse(PyObject *self, visitproc visit, void *arg);


/*
 * The type specification.
 */
static PyType_Slot Array_slots[] = {
    {Py_bf_getbuffer, Array_getbuffer},
    {Py_mp_ass_subscript, Array_ass_subscript},
    {Py_mp_length, Array_length},
    {Py_mp_subscript, Array_subscript},
    {Py_sq_item, Array_item},
    {Py_sq_length, Array_length},
    {Py_tp_clear, Array_clear},
    {Py_tp_dealloc, Array_dealloc},
    {Py_tp_new, Array_new},
    {Py_tp_repr, Array_repr},
    {Py_tp_traverse, Array_traverse},
    {0, NULL}
};

static PyType_Spec Array_TypeSpec = {
    .name = _SIP_MODULE_FQ_NAME ".array",
    .basicsize = sizeof (Array),
    .flags = Py_TPFLAGS_DEFAULT |
#if defined(Py_TPFLAGS_DISALLOW_INSTANTIATION)
             Py_TPFLAGS_DISALLOW_INSTANTIATION |
#endif
#if defined(Py_TPFLAGS_IMMUTABLETYPE)
             Py_TPFLAGS_IMMUTABLETYPE |
#endif
             Py_TPFLAGS_HAVE_GC,
    .slots = Array_slots,
};


/* Forward declarations. */
static void bad_key(PyObject *key);
static int check_index(Array *array, Py_ssize_t idx);
static int check_writable(Array *array);
static PyObject *create_array(PyTypeObject *array_type, void *data,
        sipWrappedModuleState *wms, sipTypeID type_id, const char *format,
        size_t stride, Py_ssize_t len, int flags, PyObject *owner);
static void *element(Array *array, Py_ssize_t idx);
static void *get_slice(Array *array, PyObject *value, Py_ssize_t len);
static const char *get_type_name(Array *array);
static void *get_value(Array *array, PyObject *value);


/*
 * Implement len() for the type.
 */
static Py_ssize_t Array_length(PyObject *self)
{
    return ((Array *)self)->len;
}


/*
 * Implement sequence item sub-script for the type.
 */
static PyObject *Array_item(PyObject *self, Py_ssize_t idx)
{
    Array *array = (Array *)self;
    PyObject *py_item;
    void *data;

    if (check_index(array, idx) < 0)
        return NULL;

    data = element(array, idx);

    if (sipTypeIDIsPOD(array->type_id))
    {
        // TODO Consider using a POD type ID rather than 'format'.
        switch (*array->format)
        {
        case 'b':
            py_item = PyLong_FromLong(*(char *)data);
            break;

        case 'B':
            py_item = PyLong_FromUnsignedLong(*(unsigned char *)data);
            break;

        case 'h':
            py_item = PyLong_FromLong(*(short *)data);
            break;

        case 'H':
            py_item = PyLong_FromUnsignedLong(*(unsigned short *)data);
            break;

        case 'i':
            py_item = PyLong_FromLong(*(int *)data);
            break;

        case 'I':
            py_item = PyLong_FromUnsignedLong(*(unsigned int *)data);
            break;

        case 'f':
            py_item = PyFloat_FromDouble(*(float *)data);
            break;

        case 'd':
            py_item = PyFloat_FromDouble(*(double *)data);
            break;

        default:
            py_item = NULL;
        }
    }
    else
    {
        py_item = sip_convert_from_type(array->wms, data, array->type_id,
                NULL);
    }

    return py_item;
}


/*
 * Implement mapping sub-script for the type.
 */
static PyObject *Array_subscript(PyObject *self, PyObject *key)
{
    Array *array = (Array *)self;

    if (PyIndex_Check(key))
    {
        Py_ssize_t idx = PyNumber_AsSsize_t(key, PyExc_IndexError);

        if (idx == -1 && PyErr_Occurred())
            return NULL;

        if (idx < 0)
            idx += array->len;

        return Array_item(self, idx);
    }

    if (PySlice_Check(key))
    {
        Py_ssize_t start, stop, step, slicelength;

        if (sip_api_convert_from_slice_object(key, array->len, &start, &stop, &step, &slicelength) < 0)
            return NULL;

        if (step != 1)
        {
            PyErr_SetNone(PyExc_NotImplementedError);
            return NULL;
        }

        return create_array(Py_TYPE(self), element(array, start), array->wms,
                array->type_id, array->format, array->stride, slicelength,
                (array->flags & ~SIP_OWNS_MEMORY), array->owner);
    }

    bad_key(key);

    return NULL;
}


/*
 * Implement mapping assignment sub-script for the type.
 */
static int Array_ass_subscript(PyObject *self, PyObject *key, PyObject *value)
{
    Array *array = (Array *)self;
    Py_ssize_t start, len;
    void *value_data;

    if (check_writable(array) < 0)
        return -1;

    if (PyIndex_Check(key))
    {
        start = PyNumber_AsSsize_t(key, PyExc_IndexError);

        if (start == -1 && PyErr_Occurred())
            return -1;

        if (start < 0)
            start += array->len;

        if (check_index(array, start) < 0)
            return -1;

        if ((value_data = get_value(array, value)) == NULL)
            return -1;

        len = 1;
    }
    else if (PySlice_Check(key))
    {
        Py_ssize_t stop, step;

        if (sip_api_convert_from_slice_object(key, array->len, &start, &stop, &step, &len) < 0)
            return -1;

        if (step != 1)
        {
            PyErr_SetNone(PyExc_NotImplementedError);
            return -1;
        }

        if ((value_data = get_slice(array, value, len)) == NULL)
            return -1;
    }
    else
    {
        bad_key(key);

        return -1;
    }

    if (sipTypeIDIsPOD(array->type_id))
    {
        memmove(element(array, start), value_data, len * array->stride);
    }
    else
    {
        const sipTypeDef *td = sip_get_type_def(array->wms, array->type_id,
                NULL);

        sipAssignFunc assign = ((const sipClassTypeDef *)td)->ctd_assign;
        if (assign == NULL)
        {
            // TODO Review if type names in exceptions should be FQ.
            PyErr_Format(PyExc_TypeError,
                    "a " _SIP_MODULE_FQ_NAME ".array cannot copy '%s'",
                Py_TYPE(self)->tp_name);
            return -1;
        }

        Py_ssize_t i;

        for (i = 0; i < len; ++i)
        {
            assign(array->data, start + i, value_data);
            value_data = (char *)value_data + array->stride;
        }
    }

    return 0;
}


/*
 * The buffer implementation.
 */
static int Array_getbuffer(PyObject *self, Py_buffer *view, int flags)
{
    Array *array = (Array *)self;
    const char *format;
    Py_ssize_t itemsize;

    if (view == NULL)
        return 0;

    if (((flags & PyBUF_WRITABLE) == PyBUF_WRITABLE) && (array->flags & SIP_READ_ONLY))
    {
        PyErr_SetString(PyExc_BufferError, "object is not writable");
        return -1;
    }

    view->obj = self;
    Py_INCREF(self);

    /*
     * If there is no format, ie. it is a wrapped type, then present it as
     * bytes.
     */
    if ((format = array->format) == NULL)
    {
        format = "B";
        itemsize = sizeof (unsigned char);
    }
    else
    {
        itemsize = array->stride;
    }

    view->buf = array->data;
    view->len = array->len * array->stride;
    view->readonly = (array->flags & SIP_READ_ONLY);
    view->itemsize = itemsize;

    view->format = NULL;
    if ((flags & PyBUF_FORMAT) == PyBUF_FORMAT)
        /* Note that the need for a cast is probably a Python bug. */
        view->format = (char *)format;

    view->ndim = 1;

    view->shape = NULL;
    if ((flags & PyBUF_ND) == PyBUF_ND)
        view->shape = &view->len;

    view->strides = NULL;
    if ((flags & PyBUF_STRIDES) == PyBUF_STRIDES)
        view->strides = &view->itemsize;

    view->suboffsets = NULL;
    view->internal = NULL;

    return 0;
}


/*
 * The arrays's traverse slot.
 */
static int Array_traverse(PyObject *self, visitproc visit, void *arg)
{
    Array *array = (Array *)self;

    Py_VISIT(Py_TYPE(self));
    Py_VISIT(array->owner);

    return 0;
}


/*
 * The arrays's clear slot.
 */
static int Array_clear(PyObject *self)
{
    Array *array = (Array *)self;

    Py_CLEAR(array->owner);

    return 0;
}


/*
 * The array's dealloc slot.
 */
static void Array_dealloc(PyObject *self)
{
    PyObject_GC_UnTrack(self);
    Array_clear(self);

    Array *array = (Array *)self;

    if (array->flags & SIP_OWNS_MEMORY)
    {
        if (sipTypeIDIsPOD(array->type_id))
        {
            PyMem_Free(array->data);
        }
        else
        {
            const sipTypeDef *td = sip_get_type_def(array->wms,
                    array->type_id, NULL);

            ((const sipClassTypeDef *)td)->ctd_array_delete(array->data);
        }
    }

    PyTypeObject *type = Py_TYPE(self);
    type->tp_free(self);
    Py_DECREF(type);
}


/*
 * Implement __repr__ for the type.
 */
static PyObject *Array_repr(PyObject *self)
{
    Array *array = (Array *)self;

    return PyUnicode_FromFormat(_SIP_MODULE_FQ_NAME ".array(%s, %zd)",
            get_type_name(array), array->len);
}


/*
 * Implement __new__ for the type.
 */
static PyObject *Array_new(PyTypeObject *cls, PyObject *args, PyObject *kw)
{
#if PY_VERSION_HEX >= 0x030d0000
    static char * const kwlist[] = {"", "", NULL};
#else
    static char *kwlist[] = {"", "", NULL};
#endif

    sipSipModuleState *sms = (sipSipModuleState *)PyType_GetModuleState(cls);
    sipWrapperType *wt;
    Py_ssize_t length;

    if (!PyArg_ParseTupleAndKeywords(args, kw, "O!n:array", kwlist, sms->wrapper_type_type, (PyObject **)&wt, &length))
        return NULL;

    // TODO We want to keep wt_td but not wt_type_id???
    const sipClassTypeDef *ctd = (const sipClassTypeDef *)sip_get_type_def(
            NULL, wt->wt_type_id, NULL);

    if (ctd->ctd_array == NULL || ctd->ctd_sizeof == 0)
    {
        PyErr_Format(PyExc_TypeError,
                "a " _SIP_MODULE_FQ_NAME ".array cannot be created for '%s'",
                ((PyTypeObject *)wt)->tp_name);
        return NULL;
    }

    if (length < 0)
    {
        PyErr_SetString(PyExc_ValueError,
                "a " _SIP_MODULE_FQ_NAME ".array length cannot be negative");
        return NULL;
    }

    /* Create the instance. */
    return create_array(cls, ctd->ctd_array(length), NULL, wt->wt_type_id,
            NULL, ctd->ctd_sizeof, length, SIP_OWNS_MEMORY, NULL);
}


/*
 * Return TRUE if an object is a sip.array with elements of a given type.
 */
int sip_array_can_convert(sipWrappedModuleState *wms, PyObject *obj,
        sipTypeID type_id)
{
    if (!PyObject_TypeCheck(obj, wms->sip_module_state->array_type))
        return FALSE;

    Array *array = (Array *)obj;

    return sip_get_type_def(array->wms, array->type_id, NULL) == sip_get_type_def(wms, type_id, NULL);
}


/*
 * Return the address and number of elements of a sip.array for which
 * sip_array_can_convert has already returned TRUE.
 */
void sip_array_convert(PyObject *obj, void **data, Py_ssize_t *size)
{
    Array *array = (Array *)obj;

    *data = array->data;
    *size = array->len;
}


/*
 * Initialise the array type.
 */
int sip_array_init(PyObject *module, sipSipModuleState *sms)
{
    sms->array_type = (PyTypeObject *)PyType_FromModuleAndSpec(module,
            &Array_TypeSpec, NULL);

    if (sms->array_type == NULL)
        return -1;

    if (PyModule_AddType(module, sms->array_type) < 0)
        return -1;

    return 0;
}


/*
 * Check that an array is writable.
 */
static int check_writable(Array *array)
{
    if (array->flags & SIP_READ_ONLY)
    {
        PyErr_SetString(PyExc_TypeError,
                _SIP_MODULE_FQ_NAME ".array object is read-only");
        return -1;
    }

    return 0;
}


/*
 * Check that an index is valid for an array.
 */
static int check_index(Array *array, Py_ssize_t idx)
{
    if (idx >= 0 && idx < array->len)
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
            "cannot index a " _SIP_MODULE_FQ_NAME ".array object using '%s'",
            Py_TYPE(key)->tp_name);
}


/*
 * Get the address of an element of an array.
 */
static void *element(Array *array, Py_ssize_t idx)
{
    return (unsigned char *)(array->data) + idx * array->stride;
}


/*
 * Get the address of a value that will be copied to an array.
 */
static void *get_value(Array *array, PyObject *value)
{
    static union {
        signed char s_char_t;
        unsigned char u_char_t;
        signed short s_short_t;
        unsigned short u_short_t;
        signed int s_int_t;
        unsigned int u_int_t;
        float float_t;
        double double_t;
    } static_data;

    void *data;

    if (sipTypeIDIsPOD(array->type_id))
    {
        PyErr_Clear();

        switch (*array->format)
        {
        case 'b':
            static_data.s_char_t = sip_api_long_as_char(value);
            data = &static_data.s_char_t;
            break;

        case 'B':
            static_data.u_char_t = sip_api_long_as_unsigned_char(value);
            data = &static_data.u_char_t;
            break;

        case 'h':
            static_data.s_short_t = sip_api_long_as_short(value);
            data = &static_data.s_short_t;
            break;

        case 'H':
            static_data.u_short_t = sip_api_long_as_unsigned_short(value);
            data = &static_data.u_short_t;
            break;

        case 'i':
            static_data.s_int_t = sip_api_long_as_int(value);
            data = &static_data.s_int_t;
            break;

        case 'I':
            static_data.u_int_t = sip_api_long_as_unsigned_int(value);
            data = &static_data.u_int_t;
            break;

        case 'f':
            static_data.float_t = (float)PyFloat_AsDouble(value);
            data = &static_data.float_t;
            break;

        case 'd':
            static_data.double_t = PyFloat_AsDouble(value);
            data = &static_data.double_t;
            break;

        default:
            data = NULL;
        }

        if (PyErr_Occurred())
            data = NULL;
    }
    else
    {
        int iserr = FALSE;

        data = sip_force_convert_to_type_us(array->wms, value, array->type_id,
                NULL, SIP_NOT_NONE|SIP_NO_CONVERTORS, NULL, NULL, &iserr);
    }

    return data;
}


/*
 * Get the address of an value that will be copied to an array slice.
 */
static void *get_slice(Array *array, PyObject *value, Py_ssize_t len)
{
    Array *other = (Array *)value;

    /* Check the type. */
    int bad_type = TRUE;

    if (PyObject_IsInstance(value, (PyObject *)Py_TYPE((PyObject *)array)))
    {
        if (sipTypeIDIsPOD(array->type_id))
        {
            if (sipTypeIDIsPOD(other->type_id))
            {
                if (strcmp(array->format, other->format) == 0)
                {
                    bad_type = FALSE;
                }
            }
        }
        else if (!sipTypeIDIsPOD(other->type_id))
        {
            if (sip_get_type_def(array->wms, array->type_id, NULL) == sip_get_type_def(other->wms, other->type_id, NULL))
            {
                bad_type = FALSE;
            }
        }
    }

    /* Check the dimensions. */
    if (bad_type)
    {
        PyErr_Format(PyExc_TypeError,
                "can only assign another array of %s to the slice",
                get_type_name(array));
        return NULL;
    }

    if (other->len != len)
    {
        PyErr_Format(PyExc_TypeError,
                "the array being assigned must have length %zd", len);
        return NULL;
    }

    if (other->stride == array->stride)
    {
        PyErr_Format(PyExc_TypeError,
                "the array being assigned must have stride %zu",
                array->stride);
        return NULL;
    }

    return other->data;
}


/*
 * Get the name of the type of an element of an array.
 */
static const char *get_type_name(Array *array)
{
    const char *type_name;

    if (sipTypeIDIsPOD(array->type_id))
    {
        switch (*array->format)
        {
        case 'b':
            type_name = "char";
            break;

        case 'B':
            type_name = "unsigned char";
            break;

        case 'h':
            type_name = "short";
            break;

        case 'H':
            type_name = "unsigned short";
            break;

        case 'i':
            type_name = "int";
            break;

        case 'I':
            type_name = "unsigned int";
            break;

        case 'f':
            type_name = "float";
            break;

        case 'd':
            type_name = "double";
            break;

        default:
            type_name = "";
        }
    }
    else
    {
        type_name = sip_get_type_def(array->wms, array->type_id, NULL)->td_cname;
    }

    return type_name;
}



/*
 * Create an array.
 */
static PyObject *create_array(PyTypeObject *array_type, void *data,
        sipWrappedModuleState *wms, sipTypeID type_id, const char *format,
        size_t stride, Py_ssize_t len, int flags, PyObject *owner)
{
    Array *array = (Array *)PyType_GenericAlloc(array_type, 0);

    if (array == NULL)
        return NULL;

    if (flags & SIP_OWNS_MEMORY)
        owner = (PyObject *)array;

    Py_XINCREF(owner);

    array->data = data;
    array->wms = wms;
    array->type_id = type_id;
    array->format = format;
    array->stride = stride;
    array->len = len;
    array->flags = flags;
    array->owner = owner;

    return (PyObject *)array;
}


/*
 * Wrap an array of instances of a fundamental type.  At the moment format must
 * be either "b" (char), "B" (unsigned char), "h" (short), "H" (unsigned
 * short), "i" (int), "I" (unsigned int), "f" (float) or "d" (double).
 */
PyObject *sip_api_convert_to_array(PyObject *wmod, void *data,
        const char *format, Py_ssize_t len, int flags)
{
    size_t stride;

    assert(len >= 0);

    if (data == NULL)
    {
        Py_INCREF(Py_None);
        return Py_None;
    }

    switch (*format)
    {
    case 'b':
        stride = sizeof (char);
        break;

    case 'B':
        stride = sizeof (unsigned char);
        break;

    case 'h':
        stride = sizeof (short);
        break;

    case 'H':
        stride = sizeof (unsigned short);
        break;

    case 'i':
        stride = sizeof (int);
        break;

    case 'I':
        stride = sizeof (unsigned int);
        break;

    case 'f':
        stride = sizeof (float);
        break;

    case 'd':
        stride = sizeof (double);
        break;

    default:
        PyErr_Format(PyExc_ValueError, "'%c' is not a supported format",
                format);
        return NULL;
    }

    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    return create_array(wms->sip_module_state->array_type, data, NULL, 0,
            format, stride, len, flags, NULL);
}


/*
 * Wrap an array of instances of a defined type.
 */
PyObject *sip_api_convert_to_typed_array(PyObject *wmod, void *data,
        sipTypeID type_id, const char *format, size_t stride, Py_ssize_t len,
        int flags)
{
    if (data == NULL)
    {
        Py_INCREF(Py_None);
        return Py_None;
    }

    assert(stride > 0);
    assert(len >= 0);

    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    return create_array(wms->sip_module_state->array_type, data, wms, type_id,
            format, stride, len, flags, NULL);
}


/*
 * Wrap an arbitrary block of data to an array.
 */
PyObject *sip_array_from_bytes(struct _sipSipModuleState *sms, void *data,
        Py_ssize_t size, int rw)
{
    return create_array(sms->array_type, data, NULL, 0, "B",
            sizeof (unsigned char), size, (rw ? 0 : SIP_READ_ONLY), NULL);
}
