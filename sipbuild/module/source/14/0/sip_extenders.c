/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The support for extenders.
 *
 * Copyright (c) 2026 Phil Thompson <phil@riverbankcomputing.com>
 */


#include <Python.h>

#include <string.h>

#include "sip_extenders.h"

#include "sip_callable.h"
#include "sip_core.h"
#include "sip_module.h"
#include "sip_parsers.h"
#include "sip_wrapped_module.h"


/*
 * The implementation of a stable list iterator.  This allows a list to be
 * iterated over when its contents may change during the iteration.
 */
typedef struct {
    /* The index of the next object to return. */
    Py_ssize_t next;

    /* The stable tuple of the original list. */
    PyObject *stable;
} ListIterator;


/* A handler that is invoked when a callable extender is found. */
typedef int (*CallableHandlerFunc)(sipModuleState *,
        const sipCallableSpec *, void *, PyObject **);


/* Forward declarations. */
static int call_extension(sipModuleState *x_ms,
        const sipCallableSpec *x_c_spec, void *closure, PyObject **res_p);
static int create_extension(sipModuleState *x_ms,
        const sipCallableSpec *x_c_spec, void *closure, PyObject **res_p);
static int iterate(sipModuleState *ms, const sipTypeSpec *extending_ts,
        const char *name, CallableHandlerFunc handler, void *closure,
        PyObject **res_p);
static int list_iterator_init(ListIterator *li, PyObject *list);
static PyObject *list_iterator_next(ListIterator *li);
static void list_iterator_release(ListIterator *li);


/* The closure used when looking for an extender to call. */
typedef struct {
    PyObject **p_state_p;
    PyObject *self;
    PyObject *const *args;
    Py_ssize_t nr_args;
    PyObject *kwd_names;
} CallClosure;


/*
 * Invoke any extensions to a callable until a result is obtained, an error
 * occurs or no appropriate extender was found.
 */
PyObject *sip_extend(sipModuleState *ms, PyObject **p_state_p, PyObject *self,
        PyObject *const *args, Py_ssize_t nr_args, PyObject *kwd_names,
        const sipTypeSpec *extending_ts, const char *name)
{
    CallClosure call_closure = {
        .p_state_p = p_state_p,
        .self = self,
        .args = args,
        .nr_args = nr_args,
        .kwd_names = kwd_names,
    };

    PyObject *res;

    int state = iterate(ms, extending_ts, name, call_extension, &call_closure,
            &res);

    /* The caller uses the parser state to determine if there was an error. */
    if (state < 0)
        sip_api_set_parser_error(p_state_p);

    return state > 0 ? res : NULL;
}


/*
 * The callable handler that calls the callable.
 */
static int call_extension(sipModuleState *x_ms,
        const sipCallableSpec *x_c_spec, void *closure, PyObject **res_p)
{
    CallClosure *cc = (CallClosure *)closure;

    *res_p = x_c_spec->callable_impl(x_ms, cc->p_state_p, cc->self, cc->args,
            cc->nr_args, cc->kwd_names);

    /* See if there was a result. */
    if (*res_p != NULL)
        return 1;

    /* Stop if there was an error. */
    return *cc->p_state_p == Py_None ? -1 : 0;
}


/* The closure used when looking for an extender to create. */
typedef struct {
    PyObject *self;
    const sipTypeSpec *extending_ts;
} CreateClosure;


/*
 * Return a callable object that extends a type.  Returns -1 if there was an
 * error, otherwise the object (if there is one) is returned via a pointer.
 */
int sip_get_extension_callable(sipModuleState *ms, PyObject *self,
        const sipTypeSpec *extending_ts, const char *name,
        PyObject **callable_p)
{
    CreateClosure create_closure = {
        .self = self,
        .extending_ts = extending_ts,
    };

    int state = iterate(ms, extending_ts, name, create_extension,
            &create_closure, callable_p);

    if (state < 0)
        return -1;

    if (state == 0)
        *callable_p = NULL;

    return 0;
}


/*
 * The callable handler that creates the callable.
 */
static int create_extension(sipModuleState *x_ms,
        const sipCallableSpec *x_c_spec, void *closure, PyObject **res_p)
{
    CreateClosure *cc = (CreateClosure *)closure;

    *res_p = sipCallable_New(x_ms->sip_module_state, x_c_spec,
            x_ms->wrapped_module, cc->self, cc->extending_ts);

    /* There is no need to iterated further. */
    return *res_p != NULL ? 1 : -1;
}


/*
 * Iterate over the (virtual) list of callables that extend a type and invoke a
 * handler to perform some action.  The value returned by the handler
 * determines if the iteration continues.
 */
static int iterate(sipModuleState *ms, const sipTypeSpec *extending_ts,
        const char *name, CallableHandlerFunc handler, void *closure,
        PyObject **res_p)
{
    sipSipModuleState *sms = ms->sip_module_state;

    /* Shortcut the trivial case where there is only one module. */
    if (PyList_GET_SIZE(sms->module_list) == 1)
        return 0;

    /*
     * Iterate of the list of modules allowing for the fact that a module may
     * be removed at any time.
     */
    ListIterator li;

    if (list_iterator_init(&li, sms->module_list) < 0)
        return -1;

    PyObject *x_mod;

    while ((x_mod = list_iterator_next(&li)) != NULL)
    {
        /* Don't search the originating module. */
        if (x_mod == ms->wrapped_module)
            continue;

        sipModuleState *x_ms = sip_get_module_state(x_mod);

        /* Skip if the module doesn't have any extenders. */
        const sipExtenderSpec *extenders = x_ms->module_spec->extenders;
        if (extenders == NULL)
            continue;

        while (extenders->extending_type_id != sipTypeID_Invalid)
        {
            /* See if this extender extends the type. */
            if (sip_get_type_detail(x_ms, extenders->extending_type_id, NULL, NULL) == extending_ts)
            {
                const sipCallableSpec *x_c_spec = extenders->callables;

                /* Look for a matching name. */
                while (x_c_spec->name != NULL)
                {
                    if (strcmp(x_c_spec->name, name) == 0)
                    {
                        /*
                         * Invoke the handler.  A true result means we stop
                         * iterating.
                         */
                        int state = handler(x_ms, x_c_spec, closure, res_p);

                        if (state != 0)
                        {
                            list_iterator_release(&li);
                            return state;
                        }
                    }

                    x_c_spec++;
                }
            }

            extenders++;
        }
    }

    list_iterator_release(&li);

    return 0;
}


/*
 * Initialise a list iterator.
 */
static int list_iterator_init(ListIterator *li, PyObject *list)
{
    li->next = 0;
    li->stable = PyList_AsTuple(list);

    return li->stable != NULL ? 0 : -1;
}


/*
 * Return a borrowed reference to the next object or NULL if there wasn't one.
 */
static PyObject *list_iterator_next(ListIterator *li)
{
    if (li->next >= PyTuple_GET_SIZE(li->stable))
        return NULL;

    return PyTuple_GET_ITEM(li->stable, li->next++);
}


/*
 * Release the resources of an initialised list iterator.
 */
static void list_iterator_release(ListIterator *li)
{
    Py_DECREF(li->stable);
}
