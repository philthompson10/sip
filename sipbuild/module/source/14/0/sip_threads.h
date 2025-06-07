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


#ifdef __cplusplus
extern "C" {
#endif


struct _sipWrapper;


void sip_api_end_thread(void);

int sip_get_pending(void **pp, struct _sipWrapper **op, int *fp);
int sip_is_pending(void);
PyObject *sip_wrap_instance(void *cpp,  PyTypeObject *py_type, PyObject *args,
        struct _sipWrapper *owner, int flags);


#ifdef __cplusplus
}
#endif

#endif
