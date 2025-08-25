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
static const sipWrappedVariableDef *get_static_variable_def(
        const char *utf8_name, const sipWrappedAttrsDef *wad);
static void *get_variable_address(const sipWrappedVariableDef *wvd,
        sipWrapperType *type, PyObject *instance, PyObject *mixin_name);
static const sipTypeNr *get_wrapped_type_nr_p(const sipWrappedModuleDef *wmd,
        const char *utf8_name, const sipWrappedAttrsDef *wad);
static void raise_internal_error(const sipWrappedVariableDef *wvd);


/*
 * The module getattro slot.
 */
static PyObject *ModuleWrapper_getattro(PyObject *self, PyObject *name)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            self);

    return sip_mod_con_getattro(wms, self, name,
            &wms->wrapped_module_def->attributes);
}


/*
 * The module setattro slot.
 */
static int ModuleWrapper_setattro(PyObject *self, PyObject *name,
        PyObject *value)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            self);

    return sip_mod_con_setattro(wms, self, name, value,
            &wms->wrapped_module_def->attributes);
}


/*
 * The getattro handler for modules and containers.
 */
PyObject *sip_mod_con_getattro(sipWrappedModuleState *wms, PyObject *self,
        PyObject *name, const sipWrappedAttrsDef *wad)
{
    const char *utf8_name = PyUnicode_AsUTF8(name);

    /*
     * The behaviour of static variables is that of a data descriptor and they
     * take precedence over any attributes set by the user.
     */
    const sipWrappedVariableDef *wvd = get_static_variable_def(utf8_name, wad);

    if (wvd != NULL)
        return sip_variable_get(wms, self, wvd, NULL, NULL);

    /*
     * Revert to the super-class behaviour.  This will pick up any wrapped
     * types already created and any attributes set by the user (including
     * replacements of wrapped types).
     */
    PyObject *attr = Py_TYPE(self)->tp_base->tp_getattro(self, name);
    if (attr != NULL)
        return attr;

    /* See if it is a wrapped type. */
    const sipTypeNr *type_nr_p = get_wrapped_type_nr_p(wms->wrapped_module_def,
            utf8_name, wad);
    if (type_nr_p == NULL)
        return NULL;

    PyErr_Clear();

    return Py_XNewRef((PyObject *)sip_get_local_py_type(wms, *type_nr_p));
}


/*
 * Get the value of a variable.
 */
PyObject *sip_variable_get(sipWrappedModuleState *wms, PyObject *instance,
        const sipWrappedVariableDef *wvd, sipWrapperType *type,
        PyObject *mixin_name)
{
    if (wvd->get_code != NULL)
        return wvd->get_code();

    void *addr = get_variable_address(wvd, type, instance, mixin_name);

    switch (wvd->type_id)
    {
        case sipTypeID_byte:
            return PyLong_FromLong(*(char *)addr);

        case sipTypeID_sbyte:
            return PyLong_FromLong(*(signed char *)addr);

        case sipTypeID_ubyte:
            return PyLong_FromUnsignedLong(*(unsigned char *)addr);

        case sipTypeID_short:
            return PyLong_FromLong(*(short *)addr);

        case sipTypeID_ushort:
            return PyLong_FromUnsignedLong(*(unsigned short *)addr);

        case sipTypeID_int:
            return PyLong_FromLong(*(int *)addr);

        case sipTypeID_uint:
            return PyLong_FromUnsignedLong(*(unsigned *)addr);

        case sipTypeID_long:
            return PyLong_FromLong(*(long *)addr);

        case sipTypeID_ulong:
            return PyLong_FromUnsignedLong(*(unsigned long *)addr);

        case sipTypeID_longlong:
            return PyLong_FromLongLong(*(long long *)addr);

        case sipTypeID_ulonglong:
            return PyLong_FromUnsignedLongLong(
                    *(unsigned long long *)addr);

        case sipTypeID_Py_hash_t:
            return PyLong_FromLong(*(Py_hash_t *)addr);

        case sipTypeID_Py_ssize_t:
            return PyLong_FromSsize_t(*(Py_ssize_t *)addr);

        case sipTypeID_size_t:
            return PyLong_FromSize_t(*(size_t *)addr);

        case sipTypeID_float:
            return PyFloat_FromDouble(*(float *)addr);

        case sipTypeID_double:
            return PyFloat_FromDouble(*(double *)addr);

        case sipTypeID_char:
        case sipTypeID_schar:
        case sipTypeID_uchar:
            return PyBytes_FromStringAndSize((char *)addr, 1);

        case sipTypeID_char_ascii:
            return PyUnicode_DecodeASCII((char *)addr, 1, SIP_NULLPTR);

        case sipTypeID_char_latin1:
            return PyUnicode_DecodeLatin1((char *)addr, 1, SIP_NULLPTR);

        case sipTypeID_char_utf8:
            return PyUnicode_DecodeUTF8((char *)addr, 1, SIP_NULLPTR);

        case sipTypeID_wchar:
            return PyUnicode_FromWideChar((wchar_t *)addr, 1);

        case sipTypeID_str:
        case sipTypeID_sstr:
        case sipTypeID_ustr:
        {
            const char *c_value = *(char **)addr;

            if (c_value == SIP_NULLPTR)
            {
                Py_INCREF(Py_None);
                return Py_None;
            }

            return PyBytes_FromString(c_value);
        }

        case sipTypeID_str_ascii:
        {
            const char *c_value = *(char **)addr;

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
            const char *c_value = *(char **)addr;

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
            const char *c_value = *(char **)addr;

            if (c_value == SIP_NULLPTR)
            {
                Py_INCREF(Py_None);
                return Py_None;
            }

            return PyUnicode_DecodeUTF8(c_value, strlen(c_value), NULL);
        }

        case sipTypeID_wstr:
        {
            const wchar_t *c_value = *(wchar_t **)addr;

            if (c_value == SIP_NULLPTR)
            {
                Py_INCREF(Py_None);
                return Py_None;
            }

            return PyUnicode_FromWideChar(c_value,
                    (Py_ssize_t)wcslen(c_value));
        }

        case sipTypeID_bool:
            return PyBool_FromLong(*(_Bool *)addr);

        case sipTypeID_voidptr:
            return sip_convert_from_void_ptr(wms->sip_module_state,
                    *(void **)addr);

        case sipTypeID_voidptr_const:
            return sip_convert_from_const_void_ptr(wms->sip_module_state,
                    *(const void **)addr);

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
            PyObject *c_value = *(PyObject **)addr;

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
            return PyCapsule_New(*(void **)addr, NULL, NULL);

        default:
            break;
    }

    raise_internal_error(wvd);
    return NULL;
}


/*
 * The setattro handler for modules and containers.
 */
int sip_mod_con_setattro(sipWrappedModuleState *wms, PyObject *self,
        PyObject *name, PyObject *value, const sipWrappedAttrsDef *wad)
{
    const char *utf8_name = PyUnicode_AsUTF8(name);

    const sipWrappedVariableDef *wvd = get_static_variable_def(utf8_name, wad);

    if (wvd != NULL)
        return sip_variable_set(wms, self, value, wvd, NULL, NULL);

    return Py_TYPE(self)->tp_base->tp_setattro(self, name, value);
}


/*
 * Set the value of a variable.
 */
int sip_variable_set(sipWrappedModuleState *wms, PyObject *instance,
        PyObject *value, const sipWrappedVariableDef *wvd,
        sipWrapperType *type, PyObject *mixin_name)
{
    if (value == NULL)
    {
        PyErr_Format(PyExc_AttributeError, "'%s' cannot be deleted",
                wvd->name);
        return -1;
    }

    if (wvd->set_code != NULL)
        return wvd->set_code(value);

    if (wvd->key == SIP_WV_RO)
    {
        PyErr_Format(PyExc_ValueError,
                "'%s' is a constant and cannot be modified", wvd->name);
        return -1;
    }

    void *addr = get_variable_address(wvd, type, instance, mixin_name);

    switch (wvd->type_id)
    {
        case sipTypeID_byte:
        {
            char c_value = sip_api_long_as_char(value);

            if (PyErr_Occurred())
                return -1;

            *(char *)addr = c_value;

            return 0;
        }

        case sipTypeID_sbyte:
        {
            signed char c_value = sip_api_long_as_signed_char(value);

            if (PyErr_Occurred())
                return -1;

            *(signed char *)addr = c_value;

            return 0;
        }

        case sipTypeID_ubyte:
        {
            unsigned char c_value = sip_api_long_as_unsigned_char(value);

            if (PyErr_Occurred())
                return -1;

            *(unsigned char *)addr = c_value;

            return 0;
        }

        case sipTypeID_short:
        {
            short c_value = sip_api_long_as_short(value);

            if (PyErr_Occurred())
                return -1;

            *(short *)addr = c_value;

            return 0;
        }

        case sipTypeID_ushort:
        {
            unsigned short c_value = sip_api_long_as_unsigned_short(value);

            if (PyErr_Occurred())
                return -1;

            *(unsigned short *)addr = c_value;

            return 0;
        }

        case sipTypeID_int:
        {
            int c_value = sip_api_long_as_int(value);

            if (PyErr_Occurred())
                return -1;

            *(int *)addr = c_value;

            return 0;
        }

        case sipTypeID_uint:
        {
            unsigned c_value = sip_api_long_as_unsigned_int(value);

            if (PyErr_Occurred())
                return -1;

            *(unsigned *)addr = c_value;

            return 0;
        }

        case sipTypeID_long:
        {
            long c_value = sip_api_long_as_long(value);

            if (PyErr_Occurred())
                return -1;

            *(long *)addr = c_value;

            return 0;
        }

        case sipTypeID_ulong:
        {
            unsigned long c_value = sip_api_long_as_unsigned_long(value);

            if (PyErr_Occurred())
                return -1;

            *(unsigned long *)addr = c_value;

            return 0;
        }

        case sipTypeID_longlong:
        {
            long long c_value = sip_api_long_as_long_long(value);

            if (PyErr_Occurred())
                return -1;

            *(long long *)addr = c_value;

            return 0;
        }

        case sipTypeID_ulonglong:
        {
            unsigned long long c_value = sip_api_long_as_unsigned_long_long(
                    value);

            if (PyErr_Occurred())
                return -1;

            *(unsigned long long *)addr = c_value;

            return 0;
        }


        case sipTypeID_Py_hash_t:
        {
            Py_hash_t c_value = sip_api_long_as_long(value);

            if (PyErr_Occurred())
                return -1;

            *(Py_hash_t *)addr = c_value;

            return 0;
        }

        case sipTypeID_Py_ssize_t:
        {
            Py_ssize_t c_value = sip_api_long_as_long(value);

            if (PyErr_Occurred())
                return -1;

            *(Py_ssize_t *)addr = c_value;

            return 0;
        }

        case sipTypeID_size_t:
        {
            size_t c_value = sip_api_long_as_size_t(value);

            if (PyErr_Occurred())
                return -1;

            *(size_t *)addr = c_value;

            return 0;
        }

        case sipTypeID_float:
        {
            float c_value = PyFloat_AsDouble(value);

            if (PyErr_Occurred())
                return -1;

            *(float *)addr = c_value;

            return 0;
        }

        case sipTypeID_double:
        {
            double c_value = PyFloat_AsDouble(value);

            if (PyErr_Occurred())
                return -1;

            *(double *)addr = c_value;

            return 0;
        }

        case sipTypeID_char:
        {
            char c_value = sip_api_bytes_as_char(value);

            if (PyErr_Occurred())
                return -1;

            *(char *)addr = c_value;

            return 0;
        }

        case sipTypeID_char_ascii:
        {
            char c_value = sip_api_string_as_ascii_char(value);

            if (PyErr_Occurred())
                return -1;

            *(char *)addr = c_value;

            return 0;
        }

        case sipTypeID_char_latin1:
        {
            char c_value = sip_api_string_as_latin1_char(value);

            if (PyErr_Occurred())
                return -1;

            *(char *)addr = c_value;

            return 0;
        }

        case sipTypeID_char_utf8:
        {
            char c_value = sip_api_string_as_utf8_char(value);

            if (PyErr_Occurred())
                return -1;

            *(char *)addr = c_value;

            return 0;
        }

        case sipTypeID_schar:
        {
            signed char c_value = (signed char)sip_api_bytes_as_char(value);

            if (PyErr_Occurred())
                return -1;

            *(signed char *)addr = c_value;

            return 0;
        }

        case sipTypeID_uchar:
        {
            unsigned char c_value = (unsigned char)sip_api_bytes_as_char(
                    value);

            if (PyErr_Occurred())
                return -1;

            *(unsigned char *)addr = c_value;

            return 0;
        }

        case sipTypeID_wchar:
        {
            wchar_t c_value = sip_api_string_as_wchar(value);

            if (PyErr_Occurred())
                return -1;

            *(wchar_t *)addr = c_value;

            return 0;
        }

        case sipTypeID_str:
        {
            const char *c_value = sip_api_bytes_as_string(value);

            if (PyErr_Occurred())
                return -1;

            if (sip_keep_reference(wms, NULL, wvd->key, value) < 0)
                return -1;

            *(const char **)addr = c_value;

            return 0;
        }

        case sipTypeID_str_ascii:
        {
            const char *c_value = sip_api_string_as_ascii_string(&value);

            if (PyErr_Occurred())
                return -1;

            if (sip_keep_reference(wms, NULL, wvd->key, value) < 0)
                return -1;

            *(const char **)addr = c_value;

            return 0;
        }

        case sipTypeID_str_latin1:
        {
            const char *c_value = sip_api_string_as_latin1_string(&value);

            if (PyErr_Occurred())
                return -1;

            if (sip_keep_reference(wms, NULL, wvd->key, value) < 0)
                return -1;

            *(const char **)addr = c_value;

            return 0;
        }

        case sipTypeID_str_utf8:
        {
            const char *c_value = sip_api_string_as_utf8_string(&value);

            if (PyErr_Occurred())
                return -1;

            if (sip_keep_reference(wms, NULL, wvd->key, value) < 0)
                return -1;

            *(const char **)addr = c_value;

            return 0;
        }

        case sipTypeID_sstr:
        {
            const signed char *c_value = (const signed char *)sip_api_bytes_as_string(value);

            if (PyErr_Occurred())
                return -1;

            if (sip_keep_reference(wms, NULL, wvd->key, value) < 0)
                return -1;

            *(const signed char **)addr = c_value;

            return 0;
        }

        case sipTypeID_ustr:
        {
            const unsigned char *c_value = (const unsigned char *)sip_api_bytes_as_string(value);

            if (PyErr_Occurred())
                return -1;

            if (sip_keep_reference(wms, NULL, wvd->key, value) < 0)
                return -1;

            *(const unsigned char **)addr = c_value;

            return 0;
        }

        case sipTypeID_wstr:
        {
            wchar_t *c_value = sip_api_string_as_wstring(&value);

            if (PyErr_Occurred())
                return -1;

            if (sip_keep_reference(wms, NULL, wvd->key, value) < 0)
                return -1;

            *(wchar_t **)addr = c_value;

            return 0;
        }

        case sipTypeID_bool:
        {
            _Bool c_value = sip_api_convert_to_bool(value);

            if (PyErr_Occurred())
                return -1;

            *(_Bool *)addr = c_value;

            return 0;
        }

        case sipTypeID_voidptr:
        case sipTypeID_voidptr_const:
        {
            void *c_value = sip_api_convert_to_void_ptr(value);

            if (PyErr_Occurred())
                return -1;

            *(void **)addr = c_value;

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

            Py_XDECREF(*(PyObject **)addr);
            *(PyObject **)addr = value;

            return 0;
        }

        case sipTypeID_pycapsule:
            /* Note that earlier ABIs don't seem to support capsule setters. */
            break;

        default:
            break;
    }

    raise_internal_error(wvd);
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
 * The bsearch_s() helper function for searching a variable definitions table.
 */
static int compare_static_variable(const void *key, const void *el,
        const void *context)
{
    (void)context;

    return strcmp((const char *)key, ((const sipWrappedVariableDef *)el)->name);
}


/*
 * The bsearch_s() helper function for searching a type numbers table.
 */
static int compare_type_nr(const void *key, const void *el,
        const void *context)
{
    const char *s1 = (const char *)key;
    sipTypeNr type_nr = *(const sipTypeNr *)el;
    const sipWrappedModuleDef *wmd = (const sipWrappedModuleDef *)context;

    const sipTypeDef *td = wmd->type_defs[type_nr];
    const char *s2 = strrchr(((const sipClassTypeDef *)td)->ctd_container.cod_name, '.') + 1;

    return strcmp(s1, s2);
}


/*
 * Return the variable definition for a name or NULL if there was none.
 */
static const sipWrappedVariableDef *get_static_variable_def(
        const char *utf8_name, const sipWrappedAttrsDef *wad)
{
    return (const sipWrappedVariableDef *)bsearch_s((const void *)utf8_name,
            (const void *)wad->static_variables, wad->nr_static_variables,
            sizeof (sipWrappedVariableDef), compare_static_variable, NULL);
}


/*
 * Return the C/C++ address of a variable.
 */
static void *get_variable_address(const sipWrappedVariableDef *wvd,
        sipWrapperType *type, PyObject *instance, PyObject *mixin_name)
{
    if (wvd->address_getter != NULL)
    {
        assert(type != NULL);

        /* Check that access was via an instance. */
        if (instance == NULL || instance == Py_None)
        {
            PyErr_Format(PyExc_AttributeError,
                    "%s.%s is an instance attribute",
                    ((PyTypeObject *)type)->tp_name, wvd->name);
            return NULL;
        }

        if (mixin_name != NULL)
            instance = PyObject_GetAttr(instance, mixin_name);

        /* Get the C++ instance. */
        void *instance_addr = sip_get_cpp_ptr((sipSimpleWrapper *)instance,
                type);
        if (instance_addr == NULL)
            return NULL;

        return wvd->address_getter(instance_addr);
    }

    return wvd->address;
}


/*
 * Return the type number for a name or a negative value if there was none.
 */
static const sipTypeNr *get_wrapped_type_nr_p(const sipWrappedModuleDef *wmd,
        const char *utf8_name, const sipWrappedAttrsDef *wad)
{
    return (const sipTypeNr *)bsearch_s((const void *)utf8_name,
            (const void *)wad->type_nrs, wad->nr_types, sizeof (sipTypeNr),
            compare_type_nr, (const void *)wmd);
}


/*
 * Raise an exception relating to an invalid type ID.
 */
static void raise_internal_error(const sipWrappedVariableDef *wvd)
{
    PyErr_Format(PyExc_SystemError, "'%s': unsupported type ID: 0x%04x",
            wvd->name, wvd->type_id);
}
