/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This module implements a hash table class for mapping C/C++ addresses to the
 * corresponding wrapped Python object.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#include <stdint.h>
#include <string.h>

#include "sip_object_map.h"

#include "sip_core.h"
#include "sip_module.h"
#include "sip_simple_wrapper.h"
#include "sip_wrapper_type.h"


#define hash_1(k,s) (((uintptr_t)(k)) % (s))
#define hash_2(k,s) ((s) - 2 - (hash_1((k),(s)) % ((s) - 2)))


/* Prime numbers to use as hash table sizes. */
static uintptr_t hash_primes[] = {
    521,        1031,       2053,       4099,
    8209,       16411,      32771,      65537,      131101,     262147,
    524309,     1048583,    2097169,    4194319,    8388617,    16777259,
    33554467,   67108879,   134217757,  268435459,  536870923,  1073741827,
    2147483659U,0
};


/* Forward declarations. */
static void add_aliases(sipWrappedModuleState *wms, void *addr,
        sipSimpleWrapper *val, const sipClassTypeDef *base_ctd,
        const sipClassTypeDef *ctd);
static void add_object(sipWrappedModuleState *wms, void *addr,
        sipSimpleWrapper *val);
static sipHashEntry *find_hash_entry(sipObjectMap *om, void *key);
static void *get_unguarded_pointer(sipSimpleWrapper *sw);
static sipHashEntry *new_hash_table(uintptr_t size);
static void remove_aliases(sipObjectMap *om, void *addr, sipSimpleWrapper *val,
        const sipClassTypeDef *base_ctd, const sipClassTypeDef *ctd);
static int remove_object(sipObjectMap *om, void *addr, sipSimpleWrapper *val);
static void reorganise_map(sipObjectMap *om);


/*
 * Initialise an object map.
 */
// TODO Add error handling if new_hash_table() returns NULL.
void sip_om_init(sipObjectMap *om)
{
    om->prime_idx = 0;
    om->unused = om->size = hash_primes[om->prime_idx];
    om->stale = 0;
    om->hash_array = new_hash_table(om->size);
}


/*
 * Finalise an object map.
 */
void sip_om_finalise(sipObjectMap *om)
{
    sip_api_free(om->hash_array);
}


/*
 * Allocate and initialise a new hash table.
 */
static sipHashEntry *new_hash_table(uintptr_t size)
{
    size_t nbytes;
    sipHashEntry *hashtab;

    nbytes = sizeof (sipHashEntry) * size;

    if ((hashtab = (sipHashEntry *)sip_api_malloc(nbytes)) != NULL)
        memset(hashtab,0,nbytes);

    return hashtab;
}


/*
 * Return a pointer to the hash entry that is used, or should be used, for the
 * given C/C++ address.
 */
static sipHashEntry *find_hash_entry(sipObjectMap *om, void *key)
{
    uintptr_t hash, inc;
    void *hek;

    hash = hash_1(key, om->size);
    inc = hash_2(key, om->size);

    while ((hek = om->hash_array[hash].key) != NULL && hek != key)
        hash = (hash + inc) % om->size;

    return &om->hash_array[hash];
}


/*
 * Return the wrapped Python object of a specific type for a C/C++ address or
 * NULL if it wasn't found.
 */
sipSimpleWrapper *sip_om_find_object(sipObjectMap *om, void *key,
        PyTypeObject *py_type, const sipTypeDef *td)
{
    sipHashEntry *he = find_hash_entry(om, key);
    sipSimpleWrapper *sw;

    /* Go through each wrapped object at this address. */
    for (sw = he->first; sw != NULL; sw = sw->next)
    {
        sipSimpleWrapper *unaliased;

        unaliased = (sipIsAlias(sw) ? (sipSimpleWrapper *)sw->data : sw);

        /*
         * If the reference count is 0 then it is in the process of being
         * deleted, so ignore it.  It's not completely clear how this can
         * happen (but it can) because it implies that the garbage collection
         * code is being re-entered (and there are guards in place to prevent
         * this).
         */
        if (Py_REFCNT(unaliased) == 0)
            continue;

        /* Ignore it if the C/C++ address is no longer valid. */
        if (sip_api_get_address(unaliased) == NULL)
            continue;

        /*
         * If this wrapped object is of the given type, or a sub-type of it,
         * then we assume it is the same C++ object.
         */
        if (PyObject_TypeCheck(unaliased, py_type))
            return unaliased;
    }

    return NULL;
}


/*
 * Add a C/C++ address and the corresponding wrapped Python object to the map.
 */
void sip_om_add_object(sipWrappedModuleState *wms, sipSimpleWrapper *val,
        const sipClassTypeDef *base_ctd)
{
    void *addr = get_unguarded_pointer(val);

    /* Add the object. */
    add_object(wms, addr, val);

    /* Add any aliases. */
    add_aliases(wms, addr, val, base_ctd, base_ctd);
}


/*
 * Add an alias for any address that is different when cast to a super-type.
 */
static void add_aliases(sipWrappedModuleState *wms, void *addr,
        sipSimpleWrapper *val, const sipClassTypeDef *base_ctd,
        const sipClassTypeDef *ctd)
{
    const sipTypeID *supers;

    /* See if there are any super-classes. */
    if ((supers = ctd->ctd_supers) != NULL)
    {
        sipTypeID type_id = *supers++;

        const sipClassTypeDef *sup_ctd = sip_get_generated_class_type_def(
                type_id, ctd);

        /* Recurse up the hierachy for the first super-class. */
        add_aliases(wms, addr, val, base_ctd, sup_ctd);

        /*
         * We only check for aliases for subsequent super-classes because the
         * first one can never need one.
         */
        while (!sipTypeIDIsSentinel(type_id))
        {
            type_id = *supers++;
            void *sup_addr;

            sup_ctd = sip_get_generated_class_type_def(type_id, ctd);

            /* Recurse up the hierachy for the remaining super-classes. */
            add_aliases(wms, addr, val, base_ctd, sup_ctd);

            sup_addr = (*base_ctd->ctd_cast)(addr, (sipTypeDef *)sup_ctd);

            if (sup_addr != addr)
            {
                sipSimpleWrapper *alias;

                /* Note that we silently ignore errors. */
                if ((alias = sip_api_malloc(sizeof (sipSimpleWrapper))) != NULL)
                {
                    /*
                     * An alias is basically a bit-wise copy of the Python
                     * object but only to ensure the fields we are subverting
                     * are in the right place.  An alias should never be passed
                     * to the Python API.
                     */
                    *alias = *val;

                    alias->sw_flags = (val->sw_flags & SIP_SHARE_MAP) | SIP_ALIAS;
                    alias->data = val;
                    alias->next = NULL;

                    add_object(wms, sup_addr, alias);
                }
            }
        }
    }
}


/*
 * Add a wrapper (which may be an alias) to the map.
 */
static void add_object(sipWrappedModuleState *wms, void *addr,
        sipSimpleWrapper *val)
{
    sipSipModuleState *sms = wms->sip_module_state;
    sipHashEntry *he = find_hash_entry(&sms->object_map, addr);

    /*
     * If the bucket is in use then we appear to have several objects at the
     * same address.
     */
    if (he->first != NULL)
    {
        /*
         * This can happen for three reasons.  A variable of one class can be
         * declared at the start of another class.  Therefore there are two
         * objects, of different classes, with the same address.  The second
         * reason is that the old C/C++ object has been deleted by C/C++ but we
         * didn't get to find out for some reason, and a new C/C++ instance has
         * been created at the same address.  The third reason is if we are in
         * the process of deleting a Python object but the C++ object gets
         * wrapped again because the C++ dtor called a method that has been
         * re-implemented in Python.  The absence of the SIP_SHARE_MAP flag
         * tells us that a new C++ instance has just been created and so we
         * know the second reason is the correct one so we mark the old
         * pointers as invalid and reuse the entry.  Otherwise we just add this
         * one to the existing list of objects at this address.
         */
        if (!(val->sw_flags & SIP_SHARE_MAP))
        {
            sipSimpleWrapper *sw = he->first;

            he->first = NULL;

            while (sw != NULL)
            {
                sipSimpleWrapper *next = sw->next;

                if (sipIsAlias(sw))
                {
                    sip_api_free(sw);
                }
                else
                {
                    /*
                     * We are removing it from the map here.  We first have to
                     * call the destructor as the destructor itself might end
                     * up trying to remove the wrapper and its aliases from the
                     * map.
                     */
                    sip_instance_destroyed(wms, &sw);
                }

                sw = next;
            }
        }

        val->next = he->first;
        he->first = val;

        return;
    }

    /* See if the bucket was unused or stale. */
    if (he->key == NULL)
    {
        he->key = addr;
        sms->object_map.unused--;
    }
    else
    {
        sms->object_map.stale--;
    }

    /* Add the rest of the new value. */
    he->first = val;
    val->next = NULL;

    reorganise_map(&sms->object_map);
}


/*
 * Reorganise a map if it is running short of space.
 */
static void reorganise_map(sipObjectMap *om)
{
    uintptr_t old_size, i;
    sipHashEntry *ohe, *old_tab;

    /* Don't bother if it still has more than 12% available. */
    if (om->unused > om->size >> 3)
        return;

    /*
     * If reorganising (ie. making the stale buckets unused) using the same
     * sized table would make 25% available then do that.  Otherwise use a
     * bigger table (if possible).
     */
    if (om->unused + om->stale < om->size >> 2 && hash_primes[om->prime_idx + 1] != 0)
        om->prime_idx++;

    old_size = om->size;
    old_tab = om->hash_array;

    om->unused = om->size = hash_primes[om->prime_idx];
    om->stale = 0;
    om->hash_array = new_hash_table(om->size);

    /* Transfer the entries from the old table to the new one. */
    ohe = old_tab;

    for (i = 0; i < old_size; ++i)
    {
        if (ohe -> key != NULL && ohe -> first != NULL)
        {
            *find_hash_entry(om, ohe->key) = *ohe;
            om->unused--;
        }

        ++ohe;
    }

    sip_api_free(old_tab);
}


/*
 * Remove a C/C++ object from the table.  Return 0 if it was removed
 * successfully.
 */
int sip_om_remove_object(sipObjectMap *om, sipSimpleWrapper *val,
        const sipClassTypeDef *base_ctd)
{
    void *addr;

    /* Handle the trivial case. */
    if (sipNotInMap(val))
        return 0;

    if ((addr = get_unguarded_pointer(val)) == NULL)
        return 0;

    /* Remove any aliases. */
    remove_aliases(om, addr, val, base_ctd, base_ctd);

    /* Remove the object. */
    return remove_object(om, addr, val);
}


/*
 * Remove an alias for any address that is different when cast to a super-type.
 */
static void remove_aliases(sipObjectMap *om, void *addr, sipSimpleWrapper *val,
        const sipClassTypeDef *base_ctd, const sipClassTypeDef *ctd)
{
    const sipTypeID *supers;

    /* See if there are any super-classes. */
    if ((supers = ctd->ctd_supers) != NULL)
    {
        sipTypeID type_id = *supers++;

        const sipClassTypeDef *sup_ctd = sip_get_generated_class_type_def(
                type_id, ctd);

        /* Recurse up the hierachy for the first super-class. */
        remove_aliases(om, addr, val, base_ctd, sup_ctd);

        /*
         * We only check for aliases for subsequent super-classes because the
         * first one can never need one.
         */
        while (!sipTypeIDIsSentinel(type_id))
        {
            type_id = *supers++;
            void *sup_addr;

            sup_ctd = sip_get_generated_class_type_def(type_id, ctd);

            /* Recurse up the hierachy for the remaining super-classes. */
            remove_aliases(om, addr, val, base_ctd, sup_ctd);

            sup_addr = (*base_ctd->ctd_cast)(addr, (sipTypeDef *)sup_ctd);

            if (sup_addr != addr)
                remove_object(om, sup_addr, val);
        }
    }
}


/*
 * Remove a wrapper from the map.
 */
static int remove_object(sipObjectMap *om, void *addr, sipSimpleWrapper *val)
{
    sipHashEntry *he = find_hash_entry(om, addr);
    sipSimpleWrapper **swp;

    for (swp = &he->first; *swp != NULL; swp = &(*swp)->next)
    {
        sipSimpleWrapper *sw, *next;
        int do_remove;

        sw = *swp;
        next = sw->next;

        if (sipIsAlias(sw))
        {
            if (sw->data == val)
            {
                sip_api_free(sw);
                do_remove = TRUE;
            }
            else
            {
                do_remove = FALSE;
            }
        }
        else
        {
            do_remove = (sw == val);
        }

        if (do_remove)
        {
            *swp = next;

            /*
             * If the bucket is now empty then count it as stale.  Note that we
             * do not NULL the key and count it as unused because that might
             * throw out the search for another entry that wanted to go here,
             * found it already occupied, and was put somewhere else.  In other
             * words, searches must be repeatable until we reorganise the
             * table.
             */
            if (he->first == NULL)
                om->stale++;

            return 0;
        }
    }

    return -1;
}


/*
 * Return the unguarded pointer to a C/C++ instance, ie. the pointer was valid
 * but may longer be.
 */
static void *get_unguarded_pointer(sipSimpleWrapper *sw)
{
    return (sw->access_func != NULL) ? sw->access_func(sw, UnguardedPointer) : sw->data;
}


/*
 * Call a visitor function for every wrapped object.
 */
void sip_om_visit_wrappers(sipObjectMap *om, sipWrapperVisitorFunc visitor,
        void *closure)
{
    const sipHashEntry *he;
    uintptr_t i;

    for (he = om->hash_array, i = 0; i < om->size; ++i, ++he)
    {
        if (he->key != NULL)
        {
            sipSimpleWrapper *sw;

            for (sw = he->first; sw != NULL; sw = sw->next)
                visitor(sw, closure);
        }
    }
}
