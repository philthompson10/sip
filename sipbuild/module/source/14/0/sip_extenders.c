/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The support for extenders.
 *
 * Copyright (c) 2026 Phil Thompson <phil@riverbankcomputing.com>
 */


#include <Python.h>

#include <string.h>

#include "sip_extenders.h"

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


/* Forward declarations. */
static int list_iterator_init(ListIterator *li, PyObject *list);
static PyObject *list_iterator_next(ListIterator *li);
static void list_iterator_release(ListIterator *li);


/*
 * Invoke any extensions to a callable until a result is obtained, an error
 * occurs or no appropriate extender was found.
 */
PyObject *sip_extend(sipModuleState *ms, PyObject **p_state_p, PyObject *self,
        PyObject *const *args, Py_ssize_t nr_args, PyObject *kwd_names,
        const sipTypeSpec *extending_ts, const char *name)
{
    sipSipModuleState *sms = ms->sip_module_state;

    /* Shortcut the trivial case where there is only one module. */
    if (PyList_GET_SIZE(sms->module_list) == 1)
        return NULL;

    /*
     * Iterate of the list of modules allowing for the fact that a module may
     * be removed at any time.
     */
    ListIterator li;

    if (list_iterator_init(&li, sms->module_list) < 0)
    {
        sip_api_set_parser_error(p_state_p);
        return NULL;
    }

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
                const sipCallableSpec *x_callable = extenders->callables;

                /* Look for a matching name. */
                while (x_callable->name != NULL)
                {
                    if (strcmp(x_callable->name, name) == 0)
                    {
                        /* Invoke the extender. */
                        PyObject *res = x_callable->callable_impl(x_ms,
                                p_state_p, self, args, nr_args, kwd_names);

                        /*
                         * We are done if we have a result or there was an
                         * error.
                         */
                        if (res != NULL || *p_state_p == Py_None)
                        {
                            list_iterator_release(&li);
                            return res;
                        }
                    }

                    x_callable++;
                }
            }

            extenders++;
        }
    }

    list_iterator_release(&li);

    return NULL;
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
