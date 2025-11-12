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


/* Forward references. */
static sipThread *get_current_thread(sipSipModuleState *sms, int auto_alloc);
static sipPendingDef *get_pending(sipSipModuleState *sms, int auto_alloc);


/*
 * Get the address etc. of any C/C++ object waiting to be wrapped.
 */
int sip_get_pending(sipSipModuleState *sms, void **pp, PyObject **owner_p,
        int *fp)
{
    sipPendingDef *pd;

    if ((pd = get_pending(sms, TRUE)) == NULL)
        return -1;

    *pp = pd->cpp;
    *owner_p = pd->owner;
    *fp = pd->flags;

    /* Clear in case we execute Python code before finishing this wrapping. */
    pd->cpp = NULL;

    return 0;
}


/*
 * Return TRUE if anything is pending.
 */
int sip_is_pending(sipSipModuleState *sms)
{
    sipPendingDef *pd;

    if ((pd = get_pending(sms, FALSE)) == NULL)
        return FALSE;

    return (pd->cpp != NULL);
}


/*
 * Convert a new C/C++ pointer to a Python instance.
 */
PyObject *sip_wrap_instance(sipSipModuleState *sms, void *cpp,
        PyTypeObject *py_type, PyObject *args, PyObject *owner, int flags)
{
    sipPendingDef old_pending, *pd;
    PyObject *self;

    if (cpp == NULL)
    {
        Py_INCREF(Py_None);
        return Py_None;
    }

    /*
     * Object creation can trigger the Python garbage collector which in turn
     * can execute arbitrary Python code which can then call this function
     * recursively.  Therefore we save any existing pending object before
     * setting the new one.
     */
    if ((pd = get_pending(sms, TRUE)) == NULL)
        return NULL;

    old_pending = *pd;

    pd->cpp = cpp;
    pd->owner = owner;
    pd->flags = flags;

    self = PyObject_Call((PyObject *)py_type, args, NULL);

    *pd = old_pending;

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

    sipThread *thread = get_current_thread(wms->sip_module_state, FALSE);

    if (thread != NULL)
        thread->thr_ident = 0;

    PyGILState_Release(gil);
}


/*
 * Return the pending data for the current thread, allocating it if necessary,
 * or NULL if there was an error.
 */
static sipPendingDef *get_pending(sipSipModuleState *sms, int auto_alloc)
{
    sipThread *thread;

    if ((thread = get_current_thread(sms, auto_alloc)) == NULL)
        return NULL;

    return &thread->pending;
}


/*
 * Return the thread data for the current thread, allocating it if necessary,
 * or NULL if there was an error.
 */
static sipThread *get_current_thread(sipSipModuleState *sms, int auto_alloc)
{
    sipThread *thread, *empty = NULL;
    long ident = PyThread_get_thread_ident();

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
    thread->pending.cpp = NULL;

    return thread;
}
