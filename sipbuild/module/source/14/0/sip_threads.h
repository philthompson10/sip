/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the thread support.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_THREADS_H
#define _SIP_THREADS_H


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip.h"


void sip_api_end_thread(void);

int sipGetPending(void **pp, sipWrapper **op, int *fp);
int sipIsPending(void);
PyObject *sipWrapInstance(void *cpp,  PyTypeObject *py_type, PyObject *args,
        sipWrapper *owner, int flags);


#ifdef __cplusplus
extern "C" {
#endif





#ifdef __cplusplus
}
#endif

#endif
