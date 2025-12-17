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
} sipPendingWrapDef;


/*
 * The per thread data we need to maintain.
 */
typedef struct _sipThread {
    unsigned long thr_ident;        /* The thread identifier. */
    sipPendingWrapDef pending_wrap; /* An object waiting to be wrapped. */
    PyObject **unused_args;         /* A pointer to an unused args dict. */
    struct _sipThread *next;        /* Next in the list. */
} sipThread;


void sip_api_end_thread(PyObject *w_mod);

sipThread *sip_get_thread_data(struct _sipSipModuleState *sms, int auto_alloc);
PyObject *sip_wrap_instance(struct _sipSipModuleState *sms, void *cpp,
        PyTypeObject *py_type, PyObject *args, PyObject *owner, int flags);

#ifdef __cplusplus
}
#endif

#endif
