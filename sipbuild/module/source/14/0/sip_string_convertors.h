/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for string convertors.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_STRING_CONVERTORS
#define _SIP_STRING_CONVERTORS


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>


#ifdef __cplusplus
extern "C" {
#endif


char sip_api_bytes_as_char(PyObject *obj);
const char *sip_api_bytes_as_string(PyObject *obj);
char sip_api_string_as_ascii_char(PyObject *obj);
const char *sip_api_string_as_ascii_string(PyObject **obj);
char sip_api_string_as_latin1_char(PyObject *obj);
const char *sip_api_string_as_latin1_string(PyObject **obj);
char sip_api_string_as_utf8_char(PyObject *obj);
const char *sip_api_string_as_utf8_string(PyObject **obj);
wchar_t sip_api_unicode_as_wchar(PyObject *obj);
wchar_t *sip_api_unicode_as_wstring(PyObject *obj);

int sip_parse_bytes_as_char(PyObject *obj, char *ap);
int sip_parse_bytes_as_char_array(PyObject *obj, const char **ap,
        Py_ssize_t *aszp);
int sip_parse_bytes_as_string(PyObject *obj, const char **ap);
int sip_parse_string_as_ascii_char(PyObject *obj, char *ap);
PyObject *sip_parse_string_as_ascii_string(PyObject *obj, const char **ap);
int sip_parse_string_as_latin1_char(PyObject *obj, char *ap);
PyObject *sip_parse_string_as_latin1_string(PyObject *obj, const char **ap);
int sip_parse_string_as_utf8_char(PyObject *obj, char *ap);
PyObject *sip_parse_string_as_utf8_string(PyObject *obj, const char **ap);
int sip_parse_wchar_t(PyObject *obj, wchar_t *ap);
int sip_parse_wchar_t_array(PyObject *obj, wchar_t **ap, Py_ssize_t *aszp);
int sip_parse_wchar_t_string(PyObject *obj, wchar_t **ap);


#ifdef __cplusplus
}
#endif

#endif
