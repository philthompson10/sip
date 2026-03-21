/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This is the implementation of the wrapped variable support.
 *
 * Copyright (c) 2026 Phil Thompson <phil@riverbankcomputing.com>
 */


#include <Python.h>

#include <stdbool.h>

#include "sip_variable.h"

#include "sip_core.h"
#include "sip_enum.h"
#include "sip_int_convertors.h"
#include "sip_module.h"
#include "sip_string_convertors.h"
#include "sip_voidptr.h"


/* Forward declarations. */
static void *get_variable_address(const sipAttrSpec *v_spec,
        PyTypeObject *binding_type, PyObject *instance, PyObject *mixin_name);
static void raise_internal_error(const sipAttrSpec *attr_spec);


/*
 * Get the value of a variable.
 */
PyObject *sip_variable_get(sipModuleState *ms, PyObject *instance,
        const sipAttrSpec *attr_spec, PyTypeObject *binding_type,
        PyObject *mixin_name)
{
    const sipVariableSpec *v_spec = attr_spec->spec.variable;

    if (v_spec->get_code != NULL)
        return v_spec->get_code();

    void *addr;

    if (v_spec->key == SIP_WV_LITERAL)
        addr = NULL;
    else if ((addr = get_variable_address(attr_spec, binding_type, instance, mixin_name)) == NULL)
        return NULL;

    switch (v_spec->type_id)
    {
        case sipTypeID_bool:
            return PyBool_FromLong(
                    addr != NULL ? *(bool *)addr : v_spec->value.bool_t);

        case sipTypeID_byte:
            return PyLong_FromLong(
                    addr != NULL ? *(char *)addr : v_spec->value.byte_t);

        case sipTypeID_sbyte:
            return PyLong_FromLong(
                    addr != NULL ? *(signed char *)addr : v_spec->value.sbyte_t);

        case sipTypeID_ubyte:
            return PyLong_FromUnsignedLong(
                    addr != NULL ? *(unsigned char *)addr : v_spec->value.ubyte_t);

        case sipTypeID_short:
            return PyLong_FromLong(
                    addr != NULL ? *(short *)addr : v_spec->value.short_t);

        case sipTypeID_ushort:
            return PyLong_FromUnsignedLong(
                    addr != NULL ? *(unsigned short *)addr : v_spec->value.ushort_t);

        case sipTypeID_int:
            return PyLong_FromLong(
                    addr != NULL ? *(int *)addr : v_spec->value.int_t);

        case sipTypeID_uint:
            return PyLong_FromUnsignedLong(
                    addr != NULL ? *(unsigned *)addr : v_spec->value.uint_t);

        case sipTypeID_long:
            return PyLong_FromLong(
                    addr != NULL ? *(long *)addr : v_spec->value.long_t);

        case sipTypeID_ulong:
            return PyLong_FromUnsignedLong(
                    addr != NULL ? *(unsigned long *)addr : v_spec->value.ulong_t);

        case sipTypeID_longlong:
            return PyLong_FromLongLong(
                    addr != NULL ? *(long long *)addr : v_spec->value.longlong_t);

        case sipTypeID_ulonglong:
            return PyLong_FromUnsignedLongLong(
                    addr != NULL ?  *(unsigned long long *)addr : v_spec->value.ulonglong_t);

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
                return Py_NewRef(Py_None);

            return PyBytes_FromString(c_value);
        }

        case sipTypeID_str_ascii:
        {
            const char *c_value = *(char **)addr;

            if (c_value == SIP_NULLPTR)
                return Py_NewRef(Py_None);

            return PyUnicode_DecodeASCII(c_value, strlen(c_value),
                    SIP_NULLPTR);
        }

        case sipTypeID_str_latin1:
        {
            const char *c_value = *(char **)addr;

            if (c_value == SIP_NULLPTR)
                return Py_NewRef(Py_None);

            return PyUnicode_DecodeLatin1(c_value, strlen(c_value),
                    SIP_NULLPTR);
        }

        case sipTypeID_str_utf8:
        {
            const char *c_value = *(char **)addr;

            if (c_value == SIP_NULLPTR)
                return Py_NewRef(Py_None);

            return PyUnicode_DecodeUTF8(c_value, strlen(c_value), NULL);
        }

        case sipTypeID_wstr:
        {
            const wchar_t *c_value = *(wchar_t **)addr;

            if (c_value == SIP_NULLPTR)
                return Py_NewRef(Py_None);

            return PyUnicode_FromWideChar(c_value,
                    (Py_ssize_t)wcslen(c_value));
        }

        case sipTypeID_voidptr:
            return sip_convert_from_void_ptr(ms->sip_module_state,
                    *(void **)addr);

        case sipTypeID_voidptr_const:
            return sip_convert_from_const_void_ptr(ms->sip_module_state,
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

            return Py_NewRef(c_value);
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
            if (sipTypeIDIsEnum(v_spec->type_id))
                return sip_api_convert_from_enum(ms, addr, v_spec->type_id);

            // TODO Handle classes and mapped types.
            break;
    }

    raise_internal_error(attr_spec);
    return NULL;
}


/*
 * Set the value of a variable.
 */
int sip_variable_set(sipModuleState *ms, PyObject *instance, PyObject *value,
        const sipAttrSpec *attr_spec, PyTypeObject *binding_type,
        PyObject *mixin_name)
{
    if (value == NULL)
    {
        PyErr_Format(PyExc_AttributeError, "'%s' cannot be deleted",
                attr_spec->name + 1);
        return -1;
    }

    const sipVariableSpec *v_spec = attr_spec->spec.variable;

    if (v_spec->set_code != NULL)
        return v_spec->set_code(value);

    if (v_spec->key == SIP_WV_RO || v_spec->key == SIP_WV_LITERAL)
    {
        PyErr_Format(PyExc_ValueError,
                "'%s' is a constant and cannot be modified",
                attr_spec->name + 1);
        return -1;
    }

    void *addr = get_variable_address(attr_spec, binding_type, instance,
            mixin_name);
    if (addr == NULL)
        return -1;

    switch (v_spec->type_id)
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

            if (sip_keep_reference(ms, NULL, v_spec->key, value) < 0)
                return -1;

            *(const char **)addr = c_value;

            return 0;
        }

        case sipTypeID_str_ascii:
        {
            const char *c_value = sip_api_string_as_ascii_string(&value);

            if (PyErr_Occurred())
                return -1;

            if (sip_keep_reference(ms, NULL, v_spec->key, value) < 0)
                return -1;

            *(const char **)addr = c_value;

            return 0;
        }

        case sipTypeID_str_latin1:
        {
            const char *c_value = sip_api_string_as_latin1_string(&value);

            if (PyErr_Occurred())
                return -1;

            if (sip_keep_reference(ms, NULL, v_spec->key, value) < 0)
                return -1;

            *(const char **)addr = c_value;

            return 0;
        }

        case sipTypeID_str_utf8:
        {
            const char *c_value = sip_api_string_as_utf8_string(&value);

            if (PyErr_Occurred())
                return -1;

            if (sip_keep_reference(ms, NULL, v_spec->key, value) < 0)
                return -1;

            *(const char **)addr = c_value;

            return 0;
        }

        case sipTypeID_sstr:
        {
            const signed char *c_value = (const signed char *)sip_api_bytes_as_string(value);

            if (PyErr_Occurred())
                return -1;

            if (sip_keep_reference(ms, NULL, v_spec->key, value) < 0)
                return -1;

            *(const signed char **)addr = c_value;

            return 0;
        }

        case sipTypeID_ustr:
        {
            const unsigned char *c_value = (const unsigned char *)sip_api_bytes_as_string(value);

            if (PyErr_Occurred())
                return -1;

            if (sip_keep_reference(ms, NULL, v_spec->key, value) < 0)
                return -1;

            *(const unsigned char **)addr = c_value;

            return 0;
        }

        case sipTypeID_wstr:
        {
            wchar_t *c_value = sip_api_string_as_wstring(&value);

            if (PyErr_Occurred())
                return -1;

            if (sip_keep_reference(ms, NULL, v_spec->key, value) < 0)
                return -1;

            *(wchar_t **)addr = c_value;

            return 0;
        }

        case sipTypeID_bool:
        {
            bool c_value = sip_api_convert_to_bool(value);

            if (PyErr_Occurred())
                return -1;

            *(bool *)addr = c_value;

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
            if (sipTypeIDIsEnum(v_spec->type_id))
                return sip_enum_convert_to_enum(ms, value, addr,
                        v_spec->type_id, FALSE);

            // TODO Handle classes and mapped types.
            break;
    }

    raise_internal_error(attr_spec);
    return -1;
}


/*
 * Return the C/C++ address of a variable.
 */
static void *get_variable_address(const sipAttrSpec *attr_spec,
        PyTypeObject *binding_type, PyObject *instance, PyObject *mixin_name)
{
    const sipVariableSpec *v_spec = attr_spec->spec.variable;

    if (attr_spec->name[0] == 'v')
        return v_spec->value.ptr_t;

    /* Check that access was via an instance. */
    assert(attr_spec->name[0] == 'i');

    if (instance == NULL || instance == Py_None)
    {
        PyErr_Format(PyExc_AttributeError, "%s.%s is an instance attribute",
                ((PyTypeObject *)binding_type)->tp_name, attr_spec->name + 1);
        return NULL;
    }

    if (mixin_name != NULL)
        instance = PyObject_GetAttr(instance, mixin_name);

    /* Get the C++ instance. */
    void *instance_addr = sip_get_cpp_ptr(instance, binding_type);
    if (instance_addr == NULL)
        return NULL;

    return ((sipVariableAddrGetFunc)v_spec->value.ptr_t)(instance_addr);
}


/*
 * Raise an exception relating to an invalid type ID.
 */
static void raise_internal_error(const sipAttrSpec *attr_spec)
{
    PyErr_Format(PyExc_SystemError, "'%s': unsupported type ID: 0x%04x",
            attr_spec->name + 1, attr_spec->spec.variable->type_id);
}
