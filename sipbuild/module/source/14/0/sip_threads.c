/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Thread support for the SIP library.  This module provides the hooks for
 * C++ classes that provide a thread interface to interact properly with the
 * Python threading infrastructure.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#include <Python.h>

#include "sip_threads.h"

#include "sip_core.h"
#include "sip_module.h"
#include "sip_wrapped_module.h"


/*
 * Handle the termination of a thread.
 */
void sip_api_end_thread(PyObject *w_mod)
{
    sipModuleState *wms = (sipModuleState *)PyModule_GetState(w_mod);

    PyGILState_STATE gil = PyGILState_Ensure();

    sipThread *thread = sip_get_thread_data(wms->sip_module_state, FALSE);

    if (thread != NULL)
        thread->thr_ident = 0;

    PyGILState_Release(gil);
}


/*
 * Return the thread data for the current thread.  If auto_alloc is TRUE then
 * the data will be allocated and initialised if it doesn't already exist (and
 * NULL will be returned if the allocation fails).  If auto_alloc is FALSE then
 * NULL is returned (with no exception set) if there is no data.
 */
sipThread *sip_get_thread_data(sipSipModuleState *sms, int auto_alloc)
{
    sipThread *thread, *empty = NULL;
    unsigned long ident = PyThread_get_thread_ident();

    /* See if we already know about the thread. */
    for (thread = sms->thread_list; thread != NULL; thread = thread->next)
    {
        if (thread->thr_ident == ident)
            return thread;

        if (thread->thr_ident == 0)
            empty = thread;
    }

    if (!auto_alloc)
    {
        /* This is not an error. */
        return NULL;
    }

    if (empty != NULL)
    {
        /* Use an empty entry in the list. */
        thread = empty;
    }
    else if ((thread = sip_api_malloc(sizeof (sipThread))) == NULL)
    {
        return NULL;
    }
    else
    {
        thread->next = sms->thread_list;
        sms->thread_list = thread;
    }

    thread->thr_ident = ident;
    thread->pending_wrap.cpp = NULL;
    thread->unused_args = NULL;

    return thread;
}
