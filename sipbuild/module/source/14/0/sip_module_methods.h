/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the sip module's methods.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_MODULE_METHODS_H
#define _SIP_MODULE_METHODS_H


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>


#ifdef __cplusplus
extern "C" {
#endif


extern PyMethodDef sipModuleMethods[];


#ifdef __cplusplus
}
#endif

#endif
