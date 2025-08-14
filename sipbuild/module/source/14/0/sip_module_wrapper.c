/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This is the implementation of the sip module wrapper type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <stdbool.h>
#include <string.h>

#include "sip_module_wrapper.h"

#include "sip_core.h"
#include "sip_int_convertors.h"
#include "sip_module.h"
#include "sip_string_convertors.h"
#include "sip_voidptr.h"


/* Forward declarations of slots. */
static PyObject *ModuleWrapper_getattro(PyObject *self, PyObject *name);
static int ModuleWrapper_setattro(PyObject *self, PyObject *name,
        PyObject *value);


/*
 * The type specification.
 */
static PyType_Slot ModuleWrapper_slots[] = {
    {Py_tp_getattro, ModuleWrapper_getattro},
    {Py_tp_setattro, ModuleWrapper_setattro},
    {0, NULL}
};

static PyType_Spec ModuleWrapper_TypeSpec = {
    .name = _SIP_MODULE_FQ_NAME ".modulewrapper",
    .basicsize = 0,
    .flags = Py_TPFLAGS_DEFAULT,
    .slots = ModuleWrapper_slots,
};


/* Forward declarations. */
static const void *bsearch_s(const void *key, const void *arr, size_t n,
        size_t width, int (*cmp_fn)(const void *, const void *, const void *),
        const void *context);
static int compare_static_variable(const void *key, const void *el,
        const void *context);
static int compare_type_nr(const void *key, const void *el,
        const void *context);
static const sipStaticVariableDef *get_static_variable_def(
        const sipWrappedModuleDef *wmd, const char *utf8_name);
static const size_t *get_wrapped_type_nr_p(const sipWrappedModuleDef *wmd,
        const char *utf8_name);
static void raise_internal_error(const sipStaticVariableDef *svd);


/*
 * The type getattro slot.
 */
static PyObject *ModuleWrapper_getattro(PyObject *self, PyObject *name)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            self);
    const char *utf8_name = PyUnicode_AsUTF8(name);

    /*
     * The behaviour of wrapped variables is that of a data descriptor and they
     * take precedence over any attributes set by the user.
     */
    const sipStaticVariableDef *svd = get_static_variable_def(
            wms->wrapped_module_def, utf8_name);

    if (svd == NULL)
    {
        /*
         * Revert to the super-class behaviour.  This will pick up any wrapped
         * types already created and any attributes set by the user (including
         * replacements of wrapped types).
         */
        PyObject *attr = Py_TYPE(self)->tp_base->tp_getattro(self, name);

        if (attr != NULL)
            return attr;

        /* See if it is a wrapped type. */
        const size_t *type_nr_p = get_wrapped_type_nr_p(
                wms->wrapped_module_def, utf8_name);

        if (type_nr_p == NULL)
            return NULL;

        PyErr_Clear();

        attr = (PyObject *)sip_get_local_py_type(wms, *type_nr_p);
        Py_XINCREF(attr);

        return attr;
    }

    if (svd->getter != NULL)
        return svd->getter();

    switch (svd->type_id)
    {
        case sipTypeID_byte:
            return PyLong_FromLong(*(char *)(svd->value));

        case sipTypeID_sbyte:
            return PyLong_FromLong(*(signed char *)(svd->value));

        case sipTypeID_ubyte:
            return PyLong_FromUnsignedLong(*(unsigned char *)(svd->value));

        case sipTypeID_short:
            return PyLong_FromLong(*(short *)(svd->value));

        case sipTypeID_ushort:
            return PyLong_FromUnsignedLong(*(unsigned short *)(svd->value));

        case sipTypeID_int:
            return PyLong_FromLong(*(int *)(svd->value));

        case sipTypeID_uint:
            return PyLong_FromUnsignedLong(*(unsigned *)(svd->value));

        case sipTypeID_long:
            return PyLong_FromLong(*(long *)(svd->value));

        case sipTypeID_ulong:
            return PyLong_FromUnsignedLong(*(unsigned long *)(svd->value));

        case sipTypeID_longlong:
            return PyLong_FromLongLong(*(long long *)(svd->value));

        case sipTypeID_ulonglong:
            return PyLong_FromUnsignedLongLong(
                    *(unsigned long long *)(svd->value));

        case sipTypeID_Py_hash_t:
            return PyLong_FromLong(*(Py_hash_t *)(svd->value));

        case sipTypeID_Py_ssize_t:
            return PyLong_FromSsize_t(*(Py_ssize_t *)(svd->value));

        case sipTypeID_size_t:
            return PyLong_FromSize_t(*(size_t *)(svd->value));

        case sipTypeID_float:
            return PyFloat_FromDouble(*(float *)(svd->value));

        case sipTypeID_double:
            return PyFloat_FromDouble(*(double *)(svd->value));

        case sipTypeID_char:
        case sipTypeID_schar:
        case sipTypeID_uchar:
            return PyBytes_FromStringAndSize((char *)svd->value, 1);

        case sipTypeID_char_ascii:
            return PyUnicode_DecodeASCII((char *)svd->value, 1, SIP_NULLPTR);

        case sipTypeID_char_latin1:
            return PyUnicode_DecodeLatin1((char *)svd->value, 1, SIP_NULLPTR);

        case sipTypeID_char_utf8:
            return PyUnicode_DecodeUTF8((char *)svd->value, 1, SIP_NULLPTR);

        case sipTypeID_wchar:
            return PyUnicode_FromWideChar((wchar_t *)svd->value, 1);

        case sipTypeID_str:
        case sipTypeID_sstr:
        case sipTypeID_ustr:
        {
            const char *c_value = *(char **)svd->value;

            if (c_value == SIP_NULLPTR)
            {
                Py_INCREF(Py_None);
                return Py_None;
            }

            return PyBytes_FromString(c_value);
        }

        case sipTypeID_str_ascii:
        {
            const char *c_value = *(char **)svd->value;

            if (c_value == SIP_NULLPTR)
            {
                Py_INCREF(Py_None);
                return Py_None;
            }

            return PyUnicode_DecodeASCII(c_value, strlen(c_value),
                    SIP_NULLPTR);
        }

        case sipTypeID_str_latin1:
        {
            const char *c_value = *(char **)svd->value;

            if (c_value == SIP_NULLPTR)
            {
                Py_INCREF(Py_None);
                return Py_None;
            }

            return PyUnicode_DecodeLatin1(c_value, strlen(c_value),
                    SIP_NULLPTR);
        }

        case sipTypeID_str_utf8:
        {
            const char *c_value = *(char **)svd->value;

            if (c_value == SIP_NULLPTR)
            {
                Py_INCREF(Py_None);
                return Py_None;
            }

            return PyUnicode_DecodeUTF8(c_value, strlen(c_value), NULL);
        }

        case sipTypeID_wstr:
        {
            const wchar_t *c_value = *(wchar_t **)svd->value;

            if (c_value == SIP_NULLPTR)
            {
                Py_INCREF(Py_None);
                return Py_None;
            }

            return PyUnicode_FromWideChar(c_value,
                    (Py_ssize_t)wcslen(c_value));
        }

        case sipTypeID_bool:
            return PyBool_FromLong(*(_Bool *)(svd->value));

        case sipTypeID_voidptr:
            return sip_convert_from_void_ptr(wms->sip_module_state,
                    *(void **)(svd->value));

        case sipTypeID_voidptr_const:
            return sip_convert_from_const_void_ptr(wms->sip_module_state,
                    *(const void **)(svd->value));

        case sipTypeID_pyobject:
        case sipTypeID_pytuple:
        case sipTypeID_pylist:
        case sipTypeID_pydict:
        case sipTypeID_pycallable:
        case sipTypeID_pyslice:
        case sipTypeID_pytype:
        case sipTypeID_pybuffer:
        {
            /*
             * Note that this is the historical behaviour and is probably
             * inconsistent with what the parsers do.
             */
            PyObject *c_value = *(PyObject **)svd->value;

            if (c_value == NULL)
                c_value = Py_None;

            Py_INCREF(c_value);

            return c_value;
        }

        case sipTypeID_pycapsule:
            /*
             * TODO Capsules require the type name which we don't currently
             * have access to.  The current sipTypeID implementation is
             * inadequate as it doesn't allow this information to be passed.
             * We could treat this as a new style of generated type and use a
             * module/type number approach to access its definition (ie. its
             * name).  This seems reasonable as the name is a fundamental part
             * of the type.  However are there other potential requirements for
             * specifying additional type information (eg. the size of fixed
             * arrays)?
             */
            return PyCapsule_New(*(void **)svd->value, NULL, NULL);

        default:
            break;
    }

    raise_internal_error(svd);
    return NULL;
}


/*
 * The type setattro slot.
 */
static int ModuleWrapper_setattro(PyObject *self, PyObject *name,
        PyObject *value)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            self);
    const char *utf8_name = PyUnicode_AsUTF8(name);

    const sipStaticVariableDef *svd = get_static_variable_def(
            wms->wrapped_module_def, utf8_name);

    if (svd == NULL)
        return Py_TYPE(self)->tp_base->tp_setattro(self, name, value);

    if (value == NULL)
    {
        PyErr_Format(PyExc_AttributeError, "'%s' cannot be deleted",
                svd->name);
        return -1;
    }

    if (svd->setter != NULL)
        return svd->setter(value);

    if (svd->key == SIP_SV_RO)
    {
        PyErr_Format(PyExc_ValueError,
                "'%s' is a constant and cannot be modified", svd->name);
        return -1;
    }

    switch (svd->type_id)
    {
        case sipTypeID_byte:
        {
            char c_value = sip_api_long_as_char(value);

            if (PyErr_Occurred())
                return -1;

            *(char *)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_sbyte:
        {
            signed char c_value = sip_api_long_as_signed_char(value);

            if (PyErr_Occurred())
                return -1;

            *(signed char *)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_ubyte:
        {
            unsigned char c_value = sip_api_long_as_unsigned_char(value);

            if (PyErr_Occurred())
                return -1;

            *(unsigned char *)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_short:
        {
            short c_value = sip_api_long_as_short(value);

            if (PyErr_Occurred())
                return -1;

            *(short *)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_ushort:
        {
            unsigned short c_value = sip_api_long_as_unsigned_short(value);

            if (PyErr_Occurred())
                return -1;

            *(unsigned short *)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_int:
        {
            int c_value = sip_api_long_as_int(value);

            if (PyErr_Occurred())
                return -1;

            *(int *)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_uint:
        {
            unsigned c_value = sip_api_long_as_unsigned_int(value);

            if (PyErr_Occurred())
                return -1;

            *(unsigned *)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_long:
        {
            long c_value = sip_api_long_as_long(value);

            if (PyErr_Occurred())
                return -1;

            *(long *)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_ulong:
        {
            unsigned long c_value = sip_api_long_as_unsigned_long(value);

            if (PyErr_Occurred())
                return -1;

            *(unsigned long *)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_longlong:
        {
            long long c_value = sip_api_long_as_long_long(value);

            if (PyErr_Occurred())
                return -1;

            *(long long *)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_ulonglong:
        {
            unsigned long long c_value = sip_api_long_as_unsigned_long_long(
                    value);

            if (PyErr_Occurred())
                return -1;

            *(unsigned long long *)(svd->value) = c_value;

            return 0;
        }


        case sipTypeID_Py_hash_t:
        {
            Py_hash_t c_value = sip_api_long_as_long(value);

            if (PyErr_Occurred())
                return -1;

            *(Py_hash_t *)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_Py_ssize_t:
        {
            Py_ssize_t c_value = sip_api_long_as_long(value);

            if (PyErr_Occurred())
                return -1;

            *(Py_ssize_t *)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_size_t:
        {
            size_t c_value = sip_api_long_as_size_t(value);

            if (PyErr_Occurred())
                return -1;

            *(size_t *)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_float:
        {
            float c_value = PyFloat_AsDouble(value);

            if (PyErr_Occurred())
                return -1;

            *(float *)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_double:
        {
            double c_value = PyFloat_AsDouble(value);

            if (PyErr_Occurred())
                return -1;

            *(double *)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_char:
        {
            char c_value = sip_api_bytes_as_char(value);

            if (PyErr_Occurred())
                return -1;

            *(char *)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_char_ascii:
        {
            char c_value = sip_api_string_as_ascii_char(value);

            if (PyErr_Occurred())
                return -1;

            *(char *)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_char_latin1:
        {
            char c_value = sip_api_string_as_latin1_char(value);

            if (PyErr_Occurred())
                return -1;

            *(char *)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_char_utf8:
        {
            char c_value = sip_api_string_as_utf8_char(value);

            if (PyErr_Occurred())
                return -1;

            *(char *)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_schar:
        {
            signed char c_value = (signed char)sip_api_bytes_as_char(value);

            if (PyErr_Occurred())
                return -1;

            *(signed char *)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_uchar:
        {
            unsigned char c_value = (unsigned char)sip_api_bytes_as_char(
                    value);

            if (PyErr_Occurred())
                return -1;

            *(unsigned char *)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_wchar:
        {
            wchar_t c_value = sip_api_string_as_wchar(value);

            if (PyErr_Occurred())
                return -1;

            *(wchar_t *)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_str:
        {
            const char *c_value = sip_api_bytes_as_string(value);

            if (PyErr_Occurred())
                return -1;

            if (sip_keep_reference(wms, NULL, svd->key, value) < 0)
                return -1;

            *(const char **)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_str_ascii:
        {
            const char *c_value = sip_api_string_as_ascii_string(&value);

            if (PyErr_Occurred())
                return -1;

            if (sip_keep_reference(wms, NULL, svd->key, value) < 0)
                return -1;

            *(const char **)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_str_latin1:
        {
            const char *c_value = sip_api_string_as_latin1_string(&value);

            if (PyErr_Occurred())
                return -1;

            if (sip_keep_reference(wms, NULL, svd->key, value) < 0)
                return -1;

            *(const char **)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_str_utf8:
        {
            const char *c_value = sip_api_string_as_utf8_string(&value);

            if (PyErr_Occurred())
                return -1;

            if (sip_keep_reference(wms, NULL, svd->key, value) < 0)
                return -1;

            *(const char **)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_sstr:
        {
            const signed char *c_value = (const signed char *)sip_api_bytes_as_string(value);

            if (PyErr_Occurred())
                return -1;

            if (sip_keep_reference(wms, NULL, svd->key, value) < 0)
                return -1;

            *(const signed char **)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_ustr:
        {
            const unsigned char *c_value = (const unsigned char *)sip_api_bytes_as_string(value);

            if (PyErr_Occurred())
                return -1;

            if (sip_keep_reference(wms, NULL, svd->key, value) < 0)
                return -1;

            *(const unsigned char **)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_wstr:
        {
            wchar_t *c_value = sip_api_string_as_wstring(&value);

            if (PyErr_Occurred())
                return -1;

            if (sip_keep_reference(wms, NULL, svd->key, value) < 0)
                return -1;

            *(wchar_t **)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_bool:
        {
            _Bool c_value = sip_api_convert_to_bool(value);

            if (PyErr_Occurred())
                return -1;

            *(_Bool *)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_voidptr:
        case sipTypeID_voidptr_const:
        {
            void *c_value = sip_api_convert_to_void_ptr(value);

            if (PyErr_Occurred())
                return -1;

            *(void **)(svd->value) = c_value;

            return 0;
        }

        case sipTypeID_pyobject:
        case sipTypeID_pytuple:
        case sipTypeID_pylist:
        case sipTypeID_pydict:
        case sipTypeID_pycallable:
        case sipTypeID_pyslice:
        case sipTypeID_pytype:
        case sipTypeID_pybuffer:
        {
            /*
             * Note that this is the historical behaviour and is probably
             * inconsistent with what the parsers do.
             */
            Py_INCREF(value);

            Py_XDECREF(*(PyObject **)svd->value);
            *(PyObject **)svd->value = value;

            return 0;
        }

        case sipTypeID_pycapsule:
            /* Note that earlier ABIs don't seem to support capsule setters. */
            break;

        default:
            break;
    }

    raise_internal_error(svd);
    return -1;
}


/*
 * Initialise the type.
 */
int sip_module_wrapper_init(PyObject *module, sipSipModuleState *sms)
{
    sms->module_wrapper_type = (PyTypeObject *)PyType_FromModuleAndSpec(module,
            &ModuleWrapper_TypeSpec, (PyObject *)&PyModule_Type);

    if (sms->module_wrapper_type == NULL)
        return -1;

    if (PyModule_AddType(module, sms->module_wrapper_type) < 0)
        return -1;

    return 0;
}


/*
 * An implementation of bsearch_s() as we can't rely on it being in the stdlib.
 */
static const void *bsearch_s(const void *key, const void *arr, size_t n,
        size_t width, int (*cmp_fn)(const void *, const void *, const void *),
        const void *context)
{
    if (n == 0)
        return NULL;

    size_t low = 0;
    size_t high = n - 1;

    while (low <= high)
    {
        size_t mid = low + (high - low) / 2;
        const void *el = arr + (width * mid);
        int res = cmp_fn(key, el, context);

        if (res == 0)
            return el;

        if (res > 0)
        {
            if (mid == SIZE_MAX)
                break;

            low = mid + 1;
        }
        else
        {
            if (mid == 0)
                break;

            high = mid - 1;
        }
    }

    return NULL;
}


/*
 * The bsearch_s() helper function for searching a static values table.
 */
static int compare_static_variable(const void *key, const void *el,
        const void *context)
{
    (void)context;

    return strcmp((const char *)key, ((const sipStaticVariableDef *)el)->name);
}


/*
 * The bsearch_s() helper function for searching a type numbers table.
 */
static int compare_type_nr(const void *key, const void *el,
        const void *context)
{
    const char *s1 = (const char *)key;
    size_t type_nr = *(const size_t *)el;
    const sipWrappedModuleDef *wmd = (const sipWrappedModuleDef *)context;

    const sipTypeDef *td = wmd->type_defs[type_nr];
    const char *s2 = strrchr(((const sipClassTypeDef *)td)->ctd_container.cod_name, '.') + 1;

    return strcmp(s1, s2);
}


/*
 * Return the static value definition for a name or NULL if there was none.
 */
static const sipStaticVariableDef *get_static_variable_def(
        const sipWrappedModuleDef *wmd, const char *utf8_name)
{
    return (const sipStaticVariableDef *)bsearch_s((const void *)utf8_name,
            (const void *)wmd->attributes.static_variables,
            wmd->attributes.nr_static_variables, sizeof (sipStaticVariableDef),
            compare_static_variable, NULL);
}


/*
 * Return the type number for a name or a negative value if there was none.
 */
static const size_t *get_wrapped_type_nr_p(const sipWrappedModuleDef *wmd,
        const char *utf8_name)
{
    return (const size_t *)bsearch_s((const void *)utf8_name,
            (const void *)wmd->attributes.type_nrs, wmd->attributes.nr_types,
            sizeof (size_t), compare_type_nr, (const void *)wmd);
}


/*
 * Raise an exception relating to an invalid type ID.
 */
static void raise_internal_error(const sipStaticVariableDef *svd)
{
    PyErr_Format(PyExc_SystemError, "'%s': unsupported type ID: 0x%04x",
            svd->name, svd->type_id);
}
