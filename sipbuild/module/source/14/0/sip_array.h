/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the array type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_ARRAY_H
#define _SIP_ARRAY_H


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip.h"


#ifdef __cplusplus
extern "C" {
#endif


extern PyType_Spec sipArray_TypeSpec;

PyObject *sip_api_convert_to_array(PyObject *wmod, void *data,
        const char *format, Py_ssize_t len, int flags);
PyObject *sip_api_convert_to_typed_array(PyObject *wmod, void *data,
        const sipTypeDef *td, const char *format, size_t stride,
        Py_ssize_t len, int flags);

int sip_array_can_convert(PyObject *wmod, PyObject *obj, const sipTypeDef *td);
void sip_array_convert(PyObject *obj, void **data, Py_ssize_t *size);


#ifdef __cplusplus
}
#endif

#endif
