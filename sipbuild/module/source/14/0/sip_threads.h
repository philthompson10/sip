/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the thread support.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_THREADS_H
#define _SIP_THREADS_H

#include <Python.h>


#ifdef __cplusplus
extern "C" {
#endif

struct _sipSipModuleState;


/*
 * The data associated with pending request to wrap an object.
 */
typedef struct {
    void *cpp;                      /* The C/C++ object ot be wrapped. */
    PyObject *owner;                /* The owner of the object. */
    int flags;                      /* The flags. */
} sipPendingDef;


/*
 * The per thread data we need to maintain.
 */
typedef struct _sipThread {
    long thr_ident;                 /* The thread identifier. */
    sipPendingDef pending;          /* An object waiting to be wrapped. */
    struct _sipThread *next;        /* Next in the list. */
} sipThread;


void sip_api_end_thread(PyObject *w_mod);

int sip_get_pending(struct _sipSipModuleState *sms, void **pp,
        PyObject **owner_p, int *fp);
int sip_is_pending(struct _sipSipModuleState *sms);
PyObject *sip_wrap_instance(struct _sipSipModuleState *sms, void *cpp,
        PyTypeObject *py_type, PyObject *args, PyObject *owner, int flags);

#ifdef __cplusplus
}
#endif

#endif
