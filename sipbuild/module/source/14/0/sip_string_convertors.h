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
const char *sip_api_bytes_as_char_array(PyObject *obj, Py_ssize_t *asize_p);
const char *sip_api_bytes_as_string(PyObject *obj);
char sip_api_string_as_ascii_char(PyObject *obj);
const char *sip_api_string_as_ascii_string(PyObject **obj_p);
char sip_api_string_as_latin1_char(PyObject *obj);
const char *sip_api_string_as_latin1_string(PyObject **obj_p);
char sip_api_string_as_utf8_char(PyObject *obj);
const char *sip_api_string_as_utf8_string(PyObject **obj_p);
wchar_t sip_api_string_as_wchar(PyObject *obj);
wchar_t *sip_api_string_as_wchar_array(PyObject **obj_p, Py_ssize_t *asize_p);
wchar_t *sip_api_string_as_wstring(PyObject **obj_p);


#ifdef __cplusplus
}
#endif

#endif
