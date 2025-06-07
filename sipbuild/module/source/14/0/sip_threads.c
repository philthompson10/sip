/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Thread support for the SIP library.  This module provides the hooks for
 * C++ classes that provide a thread interface to interact properly with the
 * Python threading infrastructure.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip_threads.h"

#include "sip_core.h"


/*
 * The data associated with pending request to wrap an object.
 */
typedef struct {
    void *cpp;                      /* The C/C++ object ot be wrapped. */
    sipWrapper *owner;              /* The owner of the object. */
    int flags;                      /* The flags. */
} pendingDef;


/*
 * The per thread data we need to maintain.
 */
typedef struct _threadDef {
    long thr_ident;                 /* The thread identifier. */
    pendingDef pending;             /* An object waiting to be wrapped. */
    struct _threadDef *next;        /* Next in the list. */
} threadDef;

static threadDef *threads = NULL;   /* Linked list of threads. */


/* Forward references. */
static threadDef *current_thread_def(int auto_alloc);
static pendingDef *get_pending(int auto_alloc);


/*
 * Get the address etc. of any C/C++ object waiting to be wrapped.
 */
int sip_get_pending(void **pp, sipWrapper **op, int *fp)
{
    pendingDef *pd;

    if ((pd = get_pending(TRUE)) == NULL)
        return -1;

    *pp = pd->cpp;
    *op = pd->owner;
    *fp = pd->flags;

    /* Clear in case we execute Python code before finishing this wrapping. */
    pd->cpp = NULL;

    return 0;
}


/*
 * Return TRUE if anything is pending.
 */
int sip_is_pending(void)
{
    pendingDef *pd;

    if ((pd = get_pending(FALSE)) == NULL)
        return FALSE;

    return (pd->cpp != NULL);
}


/*
 * Convert a new C/C++ pointer to a Python instance.
 */
PyObject *sip_wrap_instance(void *cpp, PyTypeObject *py_type, PyObject *args,
        sipWrapper *owner, int flags)
{
    pendingDef old_pending, *pd;
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
    if ((pd = get_pending(TRUE)) == NULL)
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
void sip_api_end_thread(void)
{
    threadDef *thread;
    PyGILState_STATE gil = PyGILState_Ensure();

    if ((thread = current_thread_def(FALSE)) != NULL)
        thread->thr_ident = 0;

    PyGILState_Release(gil);
}


/*
 * Return the pending data for the current thread, allocating it if necessary,
 * or NULL if there was an error.
 */
static pendingDef *get_pending(int auto_alloc)
{
    threadDef *thread;

    if ((thread = current_thread_def(auto_alloc)) == NULL)
        return NULL;

    return &thread->pending;
}


/*
 * Return the thread data for the current thread, allocating it if necessary,
 * or NULL if there was an error.
 */
static threadDef *current_thread_def(int auto_alloc)
{
    threadDef *thread, *empty = NULL;
    long ident = PyThread_get_thread_ident();

    /* See if we already know about the thread. */
    for (thread = threads; thread != NULL; thread = thread->next)
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
    else if ((thread = sip_api_malloc(sizeof (threadDef))) == NULL)
    {
        return NULL;
    }
    else
    {
        thread->next = threads;
        threads = thread;
    }

    thread->thr_ident = ident;
    thread->pending.cpp = NULL;

    return thread;
}
