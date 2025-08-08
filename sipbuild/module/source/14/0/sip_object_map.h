/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the object map.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_OBJECT_MAP_H
#define _SIP_OBJECT_MAP_H


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip.h"


#ifdef __cplusplus
extern "C" {
#endif


/*
 * This defines a single entry in an object map's hash table.
 */
typedef struct
{
    void *key;                  /* The C/C++ address. */
    sipSimpleWrapper *first;    /* The first object at this address. */
} sipHashEntry;


/*
 * This defines the interface to a hash table class for mapping C/C++ addresses
 * to the corresponding wrapped Python object.
 */
typedef struct
{
    int prime_idx;              /* Index into table sizes. */
    uintptr_t size;             /* Size of hash table. */
    uintptr_t unused;           /* Nr. unused in hash table. */
    uintptr_t stale;            /* Nr. stale in hash table. */
    sipHashEntry *hash_array;   /* Current hash table. */
} sipObjectMap;


void sip_om_add_object(sipWrappedModuleState *wms, sipSimpleWrapper *val);
void sip_om_finalise(sipObjectMap *om);
sipSimpleWrapper *sip_om_find_object(sipObjectMap *om, void *key,
        PyTypeObject *py_type, const sipTypeDef *td);
void sip_om_init(sipObjectMap *om);
int sip_om_remove_object(sipWrappedModuleState *wms, sipSimpleWrapper *val);
void sip_om_visit_wrappers(sipObjectMap *om, sipWrapperVisitorFunc visitor,
        void *closure);


#ifdef __cplusplus
}
#endif

#endif
