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


/*
 * Convert a new C/C++ pointer to a Python instance.
 */
PyObject *sip_wrap_instance(sipSipModuleState *sms, void *cpp,
        PyTypeObject *py_type, PyObject *args, PyObject *owner, int flags)
{
    if (cpp == NULL)
        Py_RETURN_NONE;

    /*
     * Object creation can trigger the Python garbage collector which in turn
     * can execute arbitrary Python code which can then call this function
     * recursively.  Therefore we save any existing pending wrap before setting
     * the new one.
     */
    sipThread *thread = sip_get_thread_data(sms, TRUE);
    if (thread == NULL)
        return NULL;

    sipPendingWrapDef old_pending_wrap = thread->pending_wrap;

    thread->pending_wrap.cpp = cpp;
    thread->pending_wrap.owner = owner;
    thread->pending_wrap.flags = flags;

    PyObject *self = PyObject_Call((PyObject *)py_type, args, NULL);

    thread->pending_wrap = old_pending_wrap;

    return self;
}


/*
 * Handle the termination of a thread.
 */
void sip_api_end_thread(PyObject *w_mod)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            w_mod);

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
