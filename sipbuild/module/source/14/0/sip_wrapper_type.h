/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the sip wrapper type type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_WRAPPER_TYPE_H
#define _SIP_WRAPPER_TYPE_H


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>


#ifdef __cplusplus
extern "C" {
#endif


extern PyType_Spec sipWrapperType_TypeSpec;


#ifdef __cplusplus
}
#endif

#endif
