/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the core sip module internal interfaces.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_CORE_H
#define _SIP_CORE_H


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <stdbool.h>
#include <stdint.h>

#include "sip.h"


#ifdef __cplusplus
extern "C" {
#endif

#undef  TRUE
#define TRUE        1

#undef  FALSE
#define FALSE       0


extern PyTypeObject sipWrapperType_Type;        /* The wrapper type type. */
extern sipWrapperType sipSimpleWrapper_Type;    /* The simple wrapper type. */


/*
 * These are part of the SIP API but are also used within the SIP module.
 */
void *sip_api_malloc(size_t nbytes);
void sip_api_free(void *mem);
void *sip_api_get_address(sipSimpleWrapper *w);
void *sip_api_get_cpp_ptr(sipSimpleWrapper *w, const sipTypeDef *td);
PyObject *sip_api_convert_from_type(void *cppPtr, const sipTypeDef *td,
        PyObject *transferObj);
void sip_api_instance_destroyed(sipSimpleWrapper *sipSelf);
void *sip_api_force_convert_to_type_us(PyObject *pyObj, const sipTypeDef *td,
        PyObject *transferObj, int flags, int *statep, void **user_statep,
        int *iserrp);
int sip_api_convert_from_slice_object(PyObject *slice, Py_ssize_t length,
        Py_ssize_t *start, Py_ssize_t *stop, Py_ssize_t *step,
        Py_ssize_t *slicelength);
  int sip_api_deprecated(const char *classname, const char *method);
  int sip_api_deprecated_13_9(const char *classname, const char *method, const char* message);
const sipTypeDef *sip_api_type_scope(const sipTypeDef *td);


/*
 * These are not part of the SIP API but are used within the SIP module.
 */
int sip_add_all_lazy_attrs(const sipTypeDef *td);
void sip_add_type_slots(PyHeapTypeObject *heap_to, sipPySlotDef *slots);
PyObject *sip_create_type_dict(sipExportedModuleDef *em);
int sip_dict_set_and_discard(PyObject *dict, const char *name, PyObject *obj);
void sip_fix_slots(PyTypeObject *py_type, sipPySlotDef *psd);
const sipContainerDef *sip_get_container(const sipTypeDef *td);
sipExportedModuleDef *sip_get_module(PyObject *mname_obj);
PyObject *sip_get_qualname(const sipTypeDef *td, PyObject *name);
PyObject *sip_get_scope_dict(sipTypeDef *td, PyObject *mod_dict,
        sipExportedModuleDef *client);
int sip_objectify(const char *s, PyObject **objp);

sipClassTypeDef *sipGetGeneratedClassType(const sipEncodedTypeDef *enc,
        const sipClassTypeDef *ctd);

#define sip_set_bool(p, v)    (*(_Bool *)(p) = (v))


#ifdef __cplusplus
}
#endif

#endif
