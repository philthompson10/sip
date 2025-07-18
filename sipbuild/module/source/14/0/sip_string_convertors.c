/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file implements the API for string convertors.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip_string_convertors.h"

#include "sip_core.h"


// TODO If it is possible to implement support for non-static class variables
// in the same way as other variables then these probably don't need to be part
// of the API.
/* Forward references. */
static char parse_string_as_encoded_char(PyObject *bytes, PyObject *obj);
static const char *parse_string_as_encoded_string(PyObject *bytes,
        PyObject **objp);


/*
 * Convert a Python bytes object to a C char and raise an exception if there
 * was an error.
 */
char sip_api_bytes_as_char(PyObject *obj)
{
    Py_ssize_t asize;
    const char *cp = sip_api_bytes_as_char_array(obj, &asize);

    if (PyErr_Occurred())
        return '\0';

    if (asize != 1)
        PyErr_Format(PyExc_TypeError,
                "a bytes-like object of length 1 expected");

    return *cp;
}


/*
 * Convert an optional Python bytes-like object as a C char array returning its
 * address and length.  An exception is raised if there was an error.
 */
const char *sip_api_bytes_as_char_array(PyObject *obj, Py_ssize_t *asize_p)
{
    PyErr_Clear();

    const char *cp;

    if (obj == Py_None)
    {
        cp = NULL;
        *asize_p = 0;
    }
    else if (PyBytes_Check(obj))
    {
        cp = PyBytes_AS_STRING(obj);
        *asize_p = PyBytes_GET_SIZE(obj);
    }
    else
    {
        Py_buffer view;

        if (PyObject_GetBuffer(obj, &view, PyBUF_SIMPLE) < 0)
            return NULL;

        cp = view.buf;
        *asize_p = view.len;

        PyBuffer_Release(&view);
    }

    return cp;
}


/*
 * Convert an optional Python bytes-like object to a C string and raise an
 * exception if there was an error.
 */
const char *sip_api_bytes_as_string(PyObject *obj)
{
    Py_ssize_t asize;

    return sip_api_bytes_as_char_array(obj, &asize);
}


/*
 * Convert a Python bytes-like or string object to an ASCII encoded C char and
 * raise an exception if there was an error.
 */
char sip_api_string_as_ascii_char(PyObject *obj)
{
    char ch = parse_string_as_encoded_char(PyUnicode_AsASCIIString(obj), obj);

    if (PyErr_Occurred())
    {
        /* Use the exception set if it was an encoding error. */
        if (!PyUnicode_Check(obj) || PyUnicode_GET_LENGTH(obj) != 1)
            PyErr_SetString(PyExc_TypeError,
                    "bytes-like object or ASCII string of length 1 expected");

        return '\0';
    }

    return ch;
}


/*
 * Convert an optional Python bytes-like or string object to an ASCII encoded C
 * string and raise an The object is updated with the one that owns the encoded
 * string.
 */
const char *sip_api_string_as_ascii_string(PyObject **objp)
{
    const char *cp = parse_string_as_encoded_string(
            PyUnicode_AsASCIIString(*objp), objp);

    if (PyErr_Occurred())
    {
        /* Use the exception set if it was an encoding error. */
        if (!PyUnicode_Check(*objp))
            PyErr_Format(PyExc_TypeError,
                    "bytes-like object or ASCII string expected");

        return NULL;
    }

    return cp;
}


/*
 * Convert a Python bytes-like or string object to a Latin-1 encoded C char and
 * raise an exception if there was an error.
 */
char sip_api_string_as_latin1_char(PyObject *obj)
{
    char ch = parse_string_as_encoded_char(PyUnicode_AsLatin1String(obj), obj);

    if (PyErr_Occurred())
    {
        /* Use the exception set if it was an encoding error. */
        if (!PyUnicode_Check(obj) || PyUnicode_GET_LENGTH(obj) != 1)
            PyErr_SetString(PyExc_TypeError,
                    "bytes-like object or Latin-1 string of length 1 expected");

        return '\0';
    }

    return ch;
}


/*
 * Convert an optional Python bytes-like or string object to a Latin-1 encoded
 * C string and raise an exception if there was an error.  The object is
 * updated with the one that owns the encoded string.
 */
const char *sip_api_string_as_latin1_string(PyObject **objp)
{
    const char *cp = parse_string_as_encoded_string(
            PyUnicode_AsLatin1String(*objp), objp);

    if (PyErr_Occurred())
    {
        /* Use the exception set if it was an encoding error. */
        if (!PyUnicode_Check(*objp))
            PyErr_Format(PyExc_TypeError,
                    "bytes-like object or Latin-1 string expected");

        return NULL;
    }

    return cp;
}


/*
 * Convert a Python bytes-like or string object to a UTF-8 encoded C char and
 * raise an exception if there was an error.
 */
char sip_api_string_as_utf8_char(PyObject *obj)
{
    char ch = parse_string_as_encoded_char(PyUnicode_AsUTF8String(obj), obj);

    if (PyErr_Occurred())
    {
        /* Use the exception set if it was an encoding error. */
        if (!PyUnicode_Check(obj) || PyUnicode_GET_LENGTH(obj) != 1)
            PyErr_SetString(PyExc_TypeError,
                    "bytes-like object or UTF-8 string of length 1 expected");

        return '\0';
    }

    return ch;
}


/*
 * Convert an optional Python bytes-like or string object to a UTF-8 encoded C
 * string and raise an exception if there was an error.  The object is updated
 * with the one that owns the encoded string.
 */
const char *sip_api_string_as_utf8_string(PyObject **objp)
{
    const char *cp = parse_string_as_encoded_string(
            PyUnicode_AsUTF8String(*objp), objp);

    if (PyErr_Occurred())
    {
        /* Use the exception set if it was an encoding error. */
        if (!PyUnicode_Check(*objp))
            PyErr_Format(PyExc_TypeError,
                    "bytes-like object or UTF-8 string expected");

        return NULL;
    }

    return cp;
}


/*
 * Convert a Python string object to a wchar_t character.
 */
wchar_t sip_api_string_as_wchar(PyObject *obj)
{
    wchar_t ch;

    PyErr_Clear();

    if (sip_parse_wchar(obj, &ch) < 0)
    {
        PyErr_Format(PyExc_ValueError,
                "string of length 1 expected, not %s", Py_TYPE(obj)->tp_name);

        return L'\0';
    }

    return ch;
}


/*
 * Convert a Python string object to a wide character string on the heap.
 */
wchar_t *sip_api_string_as_wstring(PyObject *obj)
{
    wchar_t *p;

    PyErr_Clear();

    if (sip_parse_wstring(obj, &p) < 0)
    {
        PyErr_Format(PyExc_ValueError,
                "string expected, not %s", Py_TYPE(obj)->tp_name);

        return NULL;
    }

    return p;
}


/*
 * Parse a string object as a wchar_t.
 */
int sip_parse_wchar(PyObject *obj, wchar_t *ap)
{
    wchar_t a;

    if (PyUnicode_Check(obj))
    {
        if (PyUnicode_GET_LENGTH(obj) != 1)
            return -1;

        if (PyUnicode_AsWideChar(obj, &a, 1) != 1)
            return -1;
    }
    else
    {
        // TODO bad type
        return -1;
    }

    if (ap != NULL)
        *ap = a;

    return 0;
}


/*
 * Parse a string object to a wchar_t array.
 */
int sip_parse_warray(PyObject *obj, wchar_t **ap, Py_ssize_t *aszp)
{
    wchar_t *a;
    Py_ssize_t asz;

    if (obj == Py_None)
    {
        a = NULL;
        asz = 0;
    }
    else if (PyUnicode_Check(obj))
    {
        if ((a = PyUnicode_AsWideCharString(obj, &asz)) == NULL)
            return -1;
    }
    else
    {
        // TODO Bad type
        return -1;
    }

    if (ap != NULL)
        *ap = a;

    if (aszp != NULL)
        *aszp = asz;

    return 0;
}


/*
 * Parse a string object to a wide character string.
 */
int sip_parse_wstring(PyObject *obj, wchar_t **ap)
{
    wchar_t *a;

    if (obj == Py_None)
    {
        a = NULL;
    }
    else if (PyUnicode_Check(obj))
    {
        if ((a = PyUnicode_AsWideCharString(obj, NULL)) == NULL)
            return -1;
    }
    else
    {
        // TODO bad type
        return -1;
    }

    if (ap != NULL)
        *ap = a;

    return 0;
}


/*
 * Parse an encoded character and return it.
 */
static char parse_string_as_encoded_char(PyObject *bytes, PyObject *obj)
{
    if (bytes == NULL)
    {
        /* Don't try anything else if there was an encoding error. */
        if (PyUnicode_Check(obj))
            return '\0';

        // TODO Update the tests for this behaviour.
        return sip_api_bytes_as_char(obj);
    }

    if (PyBytes_GET_SIZE(bytes) != 1)
    {
        PyErr_SetString(PyExc_TypeError, "decoded value of length 1 expected");

        Py_DECREF(bytes);
        return '\0';
    }

    char ch = *PyBytes_AS_STRING(bytes);
    Py_DECREF(bytes);

    /*
     * This might have been set coming in to the outer call but it didn't
     * matter until now.
     */
    PyErr_Clear();

    return ch;
}


/*
 * Parse an optional encoded string and return it and a reference to the object
 * that owns the encoded string.
 */
static const char *parse_string_as_encoded_string(PyObject *bytes,
        PyObject **objp)
{
    if (bytes == NULL)
    {
        PyObject *obj = *objp;

        /* Don't try anything else if there was an encoding error. */
        if (PyUnicode_Check(obj))
            return NULL;

        /* This will take care of the None/NULL case. */
        // TODO Update the tests for this behaviour.
        const char *cp = sip_api_bytes_as_string(obj);

        if (PyErr_Occurred())
            return NULL;

        /* The extra reference as owner. */
        Py_INCREF(obj);

        return cp;
    }

    *objp = bytes;

    /*
     * This might have been set coming in to the outer call but it didn't
     * matter until now.
     */
    PyErr_Clear();

    return PyBytes_AS_STRING(bytes);
}
