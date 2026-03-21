/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The support for extenders.
 *
 * Copyright (c) 2026 Phil Thompson <phil@riverbankcomputing.com>
 */


#include <Python.h>

#include <string.h>

#include "sip_extenders.h"

#include "sip_attribute.h"
#include "sip_module.h"
#include "sip_parsers.h"


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


/* A handler that is invoked when an attribute extender is found. */
typedef int (*AttrHandlerFunc)(sipModuleState *, const sipAttrSpec *, void *);


/* Forward declarations. */
static int call_extension(sipModuleState *x_ms,
        const sipAttrSpec *x_attr_spec, void *closure);
static int get_extension(sipModuleState *x_ms,
        const sipAttrSpec *x_attr_spec, void *closure);
static int iterate(sipModuleState *ms, const sipTypeSpec *extending_ts,
        const char *name, AttrHandlerFunc handler, void *closure);
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
    PyObject *result;
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

    int state = iterate(ms, extending_ts, name, call_extension, &call_closure);

    /* The caller uses the parser state to determine if there was an error. */
    if (state < 0)
        sip_api_set_parser_error(p_state_p);

    return state > 0 ? call_closure.result : NULL;
}


/*
 * The attribute handler that calls a callable.
 */
static int call_extension(sipModuleState *x_ms, const sipAttrSpec *x_attr_spec,
        void *closure)
{
    CallClosure *cc = (CallClosure *)closure;

    cc->result = x_attr_spec->spec.callable->callable_impl(x_ms, cc->p_state_p,
            cc->self, cc->args, cc->nr_args, cc->kwd_names);

    /* See if there was a result. */
    if (cc->result != NULL)
        return 1;

    /* Stop if there was an error. */
    return *cc->p_state_p == Py_None ? -1 : 0;
}


/* The closure used when looking for an extender attribute. */
typedef struct {
    sipModuleState *ms;
    const sipAttrSpec *attr_spec;
} GetClosure;


/*
 * Return an attribute specification that extends a type.  Returns -1 if there
 * was an error, otherwise the specification (if there is one) and the defining
 * module state are returned via pointers.
 */
int sip_get_extension_attribute(sipModuleState *ms,
        const sipTypeSpec *extending_ts, const char *name,
        sipModuleState **x_ms_p, const sipAttrSpec **x_attr_spec_p)
{
    GetClosure get_closure = {};

    if (iterate(ms, extending_ts, name, get_extension, &get_closure) < 0)
        return -1;

    *x_ms_p = get_closure.ms;
    *x_attr_spec_p = get_closure.attr_spec;

    return 0;
}


/*
 * The attribute handler that returns an attribute extension.
 */
static int get_extension(sipModuleState *x_ms, const sipAttrSpec *x_attr_spec,
        void *closure)
{
    GetClosure *gc = (GetClosure *)closure;

    gc->ms = x_ms;
    gc->attr_spec = x_attr_spec;

    /*
     * There is no need to iterate further.  Note that we don't check that
     * there aren't any other extenders for the same name.  This isn't a
     * problem for callables but other type of attribute should be unique.
     */
    return 1;
}


/*
 * Iterate over the (virtual) list of attributes that extend a type and invoke
 * a handler to perform some action.  The value returned by the handler
 * determines if the iteration continues.
 */
static int iterate(sipModuleState *ms, const sipTypeSpec *extending_ts,
        const char *name, AttrHandlerFunc handler, void *closure)
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
                const sipAttrSpec *x_attr_spec = sip_get_attribute_spec(name,
                        &extenders->attributes);

                if (x_attr_spec != NULL)
                {
                    /*
                     * Invoke the handler.  A true result means we stop
                     * iterating.
                     */
                    int state = handler(x_ms, x_attr_spec, closure);

                    if (state != 0)
                    {
                        list_iterator_release(&li);
                        return state;
                    }
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
