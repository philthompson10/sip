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

#define AUTO_DOCSTRING          '\1'    /* Marks an auto class docstring. */


/*
 * An entry in the linked list of event handlers.
 */
typedef struct _sipEventHandler {
    const sipTypeDef *td;           /* The type the handler handles. */
    void *handler;                  /* The handler. */
    struct _sipEventHandler *next;  /* The next in the list. */
} sipEventHandler;


struct _sipSipModuleState;


/*
 * These are part of the SIP API but are also used within the SIP module.
 */
int sip_api_convert_from_slice_object(PyObject *slice, Py_ssize_t length,
        Py_ssize_t *start, Py_ssize_t *stop, Py_ssize_t *step,
        Py_ssize_t *slicelength);
int sip_api_deprecated(const char *classname, const char *method);
int sip_api_deprecated_13_9(const char *classname, const char *method,
        const char *message);
int sip_api_enable_autoconversion(const sipTypeDef *td, int enable);
void sip_api_free(void *mem);
void *sip_api_get_address(sipSimpleWrapper *w);
void *sip_api_get_cpp_ptr(sipSimpleWrapper *w, const sipTypeDef *td);
void sip_api_no_function(PyObject *parseErr, const char *func,
        const char *doc);
void *sip_api_malloc(size_t nbytes);
const sipTypeDef *sip_api_type_scope(const sipTypeDef *td);


/*
 * These are not part of the SIP API but are used within the SIP module.
 */
int sip_add_all_lazy_attrs(struct _sipSipModuleState *sms,
        const sipTypeDef *td);
void sip_add_to_parent(sipWrapper *self, sipWrapper *owner);
void sip_add_type_slots(PyHeapTypeObject *heap_to, sipPySlotDef *slots);
int sip_check_pointer(void *ptr, sipSimpleWrapper *sw);
void sip_clear_access_func(sipSimpleWrapper *sw);
PyObject *sip_convert_from_type(struct _sipSipModuleState *sms, void *cppPtr,
        const sipTypeDef *td, PyObject *transferObj);
void *sip_force_convert_to_type_us(struct _sipSipModuleState *sms,
        PyObject *pyObj, const sipTypeDef *td, PyObject *transferObj,
        int flags, int *statep, void **user_statep, int *iserrp);
PyObject *sip_create_type_dict(sipExportedModuleDef *em);
int sip_dict_set_and_discard(PyObject *dict, const char *name, PyObject *obj);
void sip_fix_slots(PyTypeObject *py_type, sipPySlotDef *psd);
void sip_forget_object(sipSimpleWrapper *sw);
const sipContainerDef *sip_get_container(const sipTypeDef *td);
const sipClassTypeDef *sip_get_generated_class_type(
        const sipEncodedTypeDef *enc, const sipClassTypeDef *ctd);
sipExportedModuleDef *sip_get_module(PyObject *mname_obj);
void *sip_get_ptr_type_def(sipSimpleWrapper *self,
        const sipClassTypeDef **ctd);
PyObject *sip_get_qualname(const sipTypeDef *td, PyObject *name);
PyObject *sip_get_scope_dict(struct _sipSipModuleState *sms, sipTypeDef *td,
        PyObject *mod_dict, sipExportedModuleDef *client);
void sip_instance_destroyed(struct _sipSipModuleState *sms,
        sipSimpleWrapper **sipSelfp);
int sip_is_subtype(const sipClassTypeDef *ctd,
        const sipClassTypeDef *base_ctd);
PyObject *sip_next_in_mro(PyObject *self, PyObject *after);
int sip_objectify(const char *s, PyObject **objp);
void sip_release(void *addr, const sipTypeDef *td, int state,
        void *user_state);
void sip_remove_from_parent(sipWrapper *self);
int sip_super_init(PyObject *self, PyObject *args, PyObject *kwds,
        PyObject *type);
void sip_transfer_back(struct _sipSipModuleState *sms, PyObject *self);
void sip_transfer_to(struct _sipSipModuleState *sms, PyObject *self,
        PyObject *owner);
PyObject *sip_unpickle_type(PyObject *mod, PyObject *args);
PyObject *sip_wrap_simple_instance(struct _sipSipModuleState *sms, void *cpp,
        const sipTypeDef *td, sipWrapper *owner, int flags);

#define sip_set_bool(p, v)    (*(_Bool *)(p) = (v))


#ifdef __cplusplus
}
#endif

#endif
