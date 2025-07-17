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


/* Forward references. */
static int convert_to_wchar_t(PyObject *obj, wchar_t *ap);
static int convert_to_wchar_t_array(PyObject *obj, wchar_t **ap,
        Py_ssize_t *aszp);
static int convert_to_wchar_t_string(PyObject *obj, wchar_t **ap);
static int parse_string_as_encoded_char(PyObject *bytes, PyObject *obj,
        char *ap);
static PyObject *parse_string_as_encoded_string(PyObject *bytes, PyObject *obj,
        const char **ap);


/*
 * Convert a Python bytes object to a C char and raise an exception if there
 * was an error.
 */
char sip_api_bytes_as_char(PyObject *obj)
{
    char ch;

    PyErr_Clear();

    if (sip_parse_bytes_as_char(obj, &ch) < 0)
    {
        // TODO This is a confusing exception if object is bytes but more than
        // 1 byte.
        PyErr_Format(PyExc_TypeError, "bytes of length 1 expected not '%s'",
                Py_TYPE(obj)->tp_name);

        return '\0';
    }

    return ch;
}


/*
 * Convert a Python bytes object to a C string and raise an exception if there
 * was an error.
 */
const char *sip_api_bytes_as_string(PyObject *obj)
{
    const char *a;

    if (sip_parse_bytes_as_string(obj, &a) < 0)
    {
        PyErr_Format(PyExc_TypeError, "bytes expected not '%s'",
                Py_TYPE(obj)->tp_name);

        return NULL;
    }

    return a;
}


/*
 * Convert a Python string object to an ASCII encoded C char and raise an
 * exception if there was an error.
 */
char sip_api_string_as_ascii_char(PyObject *obj)
{
    char ch;

    PyErr_Clear();

    if (sip_parse_string_as_ascii_char(obj, &ch) < 0)
        ch = '\0';

    return ch;
}


/*
 * Convert a Python string object to an ASCII encoded C string and raise an
 * exception if there was an error.  The object is updated with the one that
 * owns the encoded string.  Note that None is considered an error.
 */
// TODO Why is None an error and not mapped to NULL? (As it is with wchar_t.)
// Is this handled elsewhere, ie. the parsers?
const char *sip_api_string_as_ascii_string(PyObject **obj)
{
    PyObject *s = *obj;
    const char *a;

    if (s == Py_None || (*obj = sip_parse_string_as_ascii_string(s, &a)) == NULL)
    {
        /* Use the exception set if it was an encoding error. */
        // TODO: Review the exception message - why bytes? Similar elsewhere.
        if (!PyUnicode_Check(s))
            PyErr_Format(PyExc_TypeError,
                    "bytes or ASCII string expected not '%s'",
                    Py_TYPE(s)->tp_name);

        return NULL;
    }

    return a;
}


/*
 * Convert a Python string object to a Latin-1 encoded C char and raise an
 * exception if there was an error.
 */
char sip_api_string_as_latin1_char(PyObject *obj)
{
    char ch;

    PyErr_Clear();

    if (sip_parse_string_as_latin1_char(obj, &ch) < 0)
        ch = '\0';

    return ch;
}


/*
 * Convert a Python string object to a Latin-1 encoded C string and raise an
 * exception if there was an error.  The object is updated with the one that
 * owns the string.  Note that None is considered an error.
 */
const char *sip_api_string_as_latin1_string(PyObject **obj)
{
    PyObject *s = *obj;
    const char *a;

    if (s == Py_None || (*obj = sip_parse_string_as_latin1_string(s, &a)) == NULL)
    {
        /* Use the exception set if it was an encoding error. */
        if (!PyUnicode_Check(s))
            PyErr_Format(PyExc_TypeError,
                    "bytes or Latin-1 string expected not '%s'",
                    Py_TYPE(s)->tp_name);

        return NULL;
    }

    return a;
}


/*
 * Convert a Python string object to a UTF-8 encoded C char and raise an
 * exception if there was an error.
 */
char sip_api_string_as_utf8_char(PyObject *obj)
{
    char ch;

    PyErr_Clear();

    if (sip_parse_string_as_utf8_char(obj, &ch) < 0)
        ch = '\0';

    return ch;
}


/*
 * Convert a Python string object to a UTF-8 encoded C string and raise an
 * exception if there was an error.  The object is updated with the one that
 * owns the string.  Note that None is considered an error.
 */
const char *sip_api_string_as_utf8_string(PyObject **obj)
{
    PyObject *s = *obj;
    const char *a;

    if (s == Py_None || (*obj = sip_parse_string_as_utf8_string(s, &a)) == NULL)
    {
        /* Use the exception set if it was an encoding error. */
        if (!PyUnicode_Check(s))
            PyErr_Format(PyExc_TypeError,
                    "bytes or UTF-8 string expected not '%s'",
                    Py_TYPE(s)->tp_name);

        return NULL;
    }

    return a;
}


/*
 * Convert a Python string object to a wchar_t character.
 */
wchar_t sip_api_unicode_as_wchar(PyObject *obj)
{
    wchar_t ch;

    PyErr_Clear();

    if (sip_parse_wchar_t(obj, &ch) < 0)
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
wchar_t *sip_api_unicode_as_wstring(PyObject *obj)
{
    wchar_t *p;

    if (sip_parse_wchar_t_string(obj, &p) < 0)
    {
        PyErr_Format(PyExc_ValueError,
                "string expected, not %s", Py_TYPE(obj)->tp_name);

        return NULL;
    }

    return p;
}


/*
 * Parse a bytes object as a C char.
 */
int sip_parse_bytes_as_char(PyObject *obj, char *ap)
{
    const char *chp;
    Py_ssize_t sz;

    if (PyBytes_Check(obj))
    {
        chp = PyBytes_AS_STRING(obj);
        sz = PyBytes_GET_SIZE(obj);
    }
    else
    {
        Py_buffer view;

        if (PyObject_GetBuffer(obj, &view, PyBUF_SIMPLE) < 0)
            return -1;

        chp = view.buf;
        sz = view.len;

        PyBuffer_Release(&view);
    }

    if (sz != 1)
        return -1;

    if (ap != NULL)
        *ap = *chp;

    return 0;
}


/*
 * Parse a bytes object as a C char array.
 */
int sip_parse_bytes_as_char_array(PyObject *obj, const char **ap,
        Py_ssize_t *aszp)
{
    const char *a;
    Py_ssize_t asz;

    if (obj == Py_None)
    {
        a = NULL;
        asz = 0;
    }
    else if (PyBytes_Check(obj))
    {
        a = PyBytes_AS_STRING(obj);
        asz = PyBytes_GET_SIZE(obj);
    }
    else
    {
        Py_buffer view;

        if (PyObject_GetBuffer(obj, &view, PyBUF_SIMPLE) < 0)
            return -1;

        a = view.buf;
        asz = view.len;

        PyBuffer_Release(&view);
    }

    if (ap != NULL)
        *ap = a;

    if (aszp != NULL)
        *aszp = asz;

    return 0;
}


/*
 * Parse a bytes object as a C string.
 */
int sip_parse_bytes_as_string(PyObject *obj, const char **ap)
{
    const char *a;
    Py_ssize_t sz;

    if (sip_parse_bytes_as_char_array(obj, &a, &sz) < 0)
        return -1;

    if (ap != NULL)
        *ap = a;

    return 0;
}


/*
 * Parse a string object as an ASCII encoded C char.
 */
int sip_parse_string_as_ascii_char(PyObject *obj, char *ap)
{
    if (parse_string_as_encoded_char(PyUnicode_AsASCIIString(obj), obj, ap) < 0)
    {
        /* Use the exception set if it was an encoding error. */
        if (!PyUnicode_Check(obj) || PyUnicode_GET_LENGTH(obj) != 1)
            PyErr_SetString(PyExc_TypeError,
                    "bytes or ASCII string of length 1 expected");

        return -1;
    }

    return 0;
}


/*
 * Parse a string object as an ASCII encoded C string returning a new reference
 * to the object that owns the encoded string.
 */
PyObject *sip_parse_string_as_ascii_string(PyObject *obj, const char **ap)
{
    return parse_string_as_encoded_string(PyUnicode_AsASCIIString(obj), obj,
            ap);
}


/*
 * Parse a string object as a Latin-1 encoded C char.
 */
int sip_parse_string_as_latin1_char(PyObject *obj, char *ap)
{
    if (parse_string_as_encoded_char(PyUnicode_AsLatin1String(obj), obj, ap) < 0)
    {
        /* Use the exception set if it was an encoding error. */
        if (!PyUnicode_Check(obj) || PyUnicode_GET_LENGTH(obj) != 1)
            PyErr_SetString(PyExc_TypeError,
                    "bytes or Latin-1 string of length 1 expected");

        return -1;
    }

    return 0;
}


/*
 * Parse a string object as a Latin-1 encoded C string returning a new
 * reference to the object that owns the encoded string.
 */
PyObject *sip_parse_string_as_latin1_string(PyObject *obj, const char **ap)
{
    return parse_string_as_encoded_string(PyUnicode_AsLatin1String(obj), obj,
            ap);
}


/*
 * Parse a string object as a UTF-8 encoded C char.
 */
int sip_parse_string_as_utf8_char(PyObject *obj, char *ap)
{
    if (parse_string_as_encoded_char(PyUnicode_AsUTF8String(obj), obj, ap) < 0)
    {
        /* Use the exception set if it was an encoding error. */
        if (!PyUnicode_Check(obj) || PyUnicode_GET_LENGTH(obj) != 1)
            PyErr_SetString(PyExc_TypeError,
                    "bytes or UTF-8 string of length 1 expected");

        return -1;
    }

    return 0;
}


/*
 * Parse a string object as a UTF-8 encoded C string returning a new reference
 * to the object that owns the encoded string.
 */
PyObject *sip_parse_string_as_utf8_string(PyObject *obj, const char **ap)
{
    return parse_string_as_encoded_string(PyUnicode_AsUTF8String(obj), obj,
            ap);
}


/*
 * Parse a string object as a wchar_t character.
 */
int sip_parse_wchar_t(PyObject *obj, wchar_t *ap)
{
    wchar_t a;

    if (PyUnicode_Check(obj))
    {
        if (convert_to_wchar_t(obj, &a) < 0)
            return -1;
    }
    else
    {
        return -1;
    }

    if (ap != NULL)
        *ap = a;

    return 0;
}


/*
 * Parse a string object to a wchar_t character array.
 */
int sip_parse_wchar_t_array(PyObject *obj, wchar_t **ap, Py_ssize_t *aszp)
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
        if (convert_to_wchar_t_array(obj, &a, &asz) < 0)
            return -1;
    }
    else
    {
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
int sip_parse_wchar_t_string(PyObject *obj, wchar_t **ap)
{
    wchar_t *a;

    if (obj == Py_None)
    {
        a = NULL;
    }
    else if (PyUnicode_Check(obj))
    {
        if (convert_to_wchar_t_string(obj, &a) < 0)
            return -1;
    }
    else
    {
        return -1;
    }

    if (ap != NULL)
        *ap = a;

    return 0;
}


/*
 * Convert a Unicode object to a wchar_t character and return it.
 */
static int convert_to_wchar_t(PyObject *obj, wchar_t *ap)
{
    if (PyUnicode_GET_LENGTH(obj) != 1)
        return -1;

    if (PyUnicode_AsWideChar(obj, ap, 1) != 1)
        return -1;

    return 0;
}


/*
 * Convert a Unicode object to a wide character array and return it's address
 * and length.
 */
static int convert_to_wchar_t_array(PyObject *obj, wchar_t **ap,
        Py_ssize_t *aszp)
{
    Py_ssize_t ulen;
    wchar_t *wc;

    ulen = PyUnicode_GET_LENGTH(obj);

    if ((wc = sip_api_malloc(ulen * sizeof (wchar_t))) == NULL)
        return -1;

    if ((ulen = PyUnicode_AsWideChar(obj, wc, ulen)) < 0)
    {
        sip_api_free(wc);
        return -1;
    }

    *ap = wc;
    *aszp = ulen;

    return 0;
}


/*
 * Convert a Unicode object to a wide character string and return a copy on
 * the heap.
 */
static int convert_to_wchar_t_string(PyObject *obj, wchar_t **ap)
{
    Py_ssize_t ulen;
    wchar_t *wc;

    ulen = PyUnicode_GET_LENGTH(obj);

    if ((wc = sip_api_malloc((ulen + 1) * sizeof (wchar_t))) == NULL)
        return -1;

    if ((ulen = PyUnicode_AsWideChar(obj, wc, ulen)) < 0)
    {
        sip_api_free(wc);
        return -1;
    }

    wc[ulen] = L'\0';

    *ap = wc;

    return 0;
}


/*
 * Parse an encoded character and return it.
 */
static int parse_string_as_encoded_char(PyObject *bytes, PyObject *obj,
        char *ap)
{
    Py_ssize_t size;

    if (bytes == NULL)
    {
        // TODO Review this, shouldn't it just raise the exception?  It looks
        // like, for historical reasons(?) we allow bytes (already encoded
        // presumably) as well as a string object.  Need to update the tests to
        // check this support.
        PyErr_Clear();

        return sip_parse_bytes_as_char(obj, ap);
    }

    size = PyBytes_GET_SIZE(bytes);

    if (size != 1)
    {
        PyErr_SetString(PyExc_TypeError, "decoded value of length 1 expected");

        Py_DECREF(bytes);
        return -1;
    }

    if (ap != NULL)
        *ap = *PyBytes_AS_STRING(bytes);

    Py_DECREF(bytes);

    return 0;
}


/*
 * Parse an encoded string and return it and a new reference to the object that
 * owns the encoded string.
 */
static PyObject *parse_string_as_encoded_string(PyObject *bytes, PyObject *obj,
        const char **ap)
{
    if (bytes != NULL)
    {
        *ap = PyBytes_AS_STRING(bytes);

        return bytes;
    }

    /* Don't try anything else if there was an encoding error. */
    if (PyUnicode_Check(obj))
        return NULL;

    PyErr_Clear();

    if (sip_parse_bytes_as_string(obj, ap) < 0)
        return NULL;

    Py_INCREF(obj);

    return obj;
}
