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


/* Macros to access the parts of a valid type ID. */
#define sipTypeIDIsSentinel(id)         ((id) & SIP_TYPE_ID_SENTINEL)
#define sipTypeIDIsGeneratedType(id)    ((id) & SIP_TYPE_ID_GENERATED)
#define sipTypeIDIsAbsolute(id)         ((id) & SIP_TYPE_ID_ABSOLUTE)
#define sipTypeIDIsExternal(id)         ((id) & SIP_TYPE_ID_EXTERNAL)
#define sipTypeIDIsCurrentModule(id)    ((id) & SIP_TYPE_ID_CURRENT_MODULE)
#define sipTypeIDTypeNr(id)             ((id) & 0xffff)
#define sipTypeIDModuleNr(id)           (((id) >> 16) & 0xff)


/*
 * An entry in the linked list of event handlers.
 */
typedef struct _sipEventHandler {
    const sipTypeDef *td;           /* The type the handler handles. */
    void *handler;                  /* The handler. */
    struct _sipEventHandler *next;  /* The next in the list. */
} sipEventHandler;


/*
 * An entry in a linked list of name/symbol pairs.
 */
typedef struct _sipSymbol {
    const char *name;               /* The name. */
    void *symbol;                   /* The symbol. */
    struct _sipSymbol *next;        /* The next in the list. */
} sipSymbol;


/*
 * The function pointers that implement the API.
 */
extern const sipAPIDef sip_api;


/*
 * These are part of the SIP API but are also used within the SIP module.
 */
int sip_api_convert_from_slice_object(PyObject *slice, Py_ssize_t length,
        Py_ssize_t *start, Py_ssize_t *stop, Py_ssize_t *step,
        Py_ssize_t *slicelength);
int sip_api_deprecated(const char *classname, const char *method,
        const char *message);
int sip_api_enable_autoconversion(sipWrapperType *wt, int enable);
void sip_api_free(void *mem);
void *sip_api_get_address(sipSimpleWrapper *w);
void *sip_api_get_cpp_ptr(PyObject *wmod, sipSimpleWrapper *w,
        sipTypeID type_id);
void *sip_api_malloc(size_t nbytes);


/*
 * These are not part of the SIP API but are used within the SIP module.
 */
void sip_add_to_parent(sipWrapper *self, sipWrapper *owner);
void sip_add_type_slots(PyHeapTypeObject *heap_to, const sipPySlotDef *slots);
int sip_append_py_object_to_list(PyObject **listp, PyObject *object);
void *sip_cast_cpp_ptr(void *ptr, PyTypeObject *src_type,
        const sipTypeDef *dst_type);
int sip_check_pointer(void *ptr, sipSimpleWrapper *sw);
void sip_clear_access_func(sipSimpleWrapper *sw);
PyTypeObject *sip_create_mapped_type(sipSipModuleState *sms,
        const sipWrappedModuleDef *wmd, const sipMappedTypeDef *mtd,
        PyObject *wmod_dict);
PyObject *sip_create_type_dict(const sipWrappedModuleDef *wmd);
int sip_dict_set_and_discard(PyObject *dict, const char *name, PyObject *obj);
void sip_fix_slots(PyTypeObject *py_type, sipPySlotDef *psd);
void sip_forget_object(sipSimpleWrapper *sw);
const sipContainerDef *sip_get_container(const sipTypeDef *td);
void *sip_get_complex_cpp_ptr(sipWrappedModuleState *wms, sipSimpleWrapper *w,
        sipTypeID type_id);
void *sip_get_cpp_ptr(sipWrappedModuleState *wms, sipSimpleWrapper *sw,
        sipTypeID type_id);
void *sip_get_final_address(sipSipModuleState *sms, const sipTypeDef *td,
        void *cpp);
sipConvertFromFunc sip_get_from_convertor(PyTypeObject *py_type,
        const sipTypeDef *td);
const sipClassTypeDef *sip_get_generated_class_type_def(sipTypeID type_id,
        const sipClassTypeDef *ctd);
PyTypeObject *sip_get_local_py_type(sipWrappedModuleState *wms,
        Py_ssize_t type_nr);
PyTypeObject *sip_get_py_type_and_type_def(sipWrappedModuleState *wms,
        sipTypeID type_id, const sipTypeDef **tdp);
const sipTypeDef *sip_get_type_def(sipWrappedModuleState *wms,
        sipTypeID type_id);
void *sip_get_ptr_type_def(sipSimpleWrapper *self,
        const sipClassTypeDef **ctd);
PyTypeObject *sip_get_py_type(sipWrappedModuleState *wms, sipTypeID type_id);
PyTypeObject *sip_get_py_type_from_name(sipSipModuleState *sms,
        PyObject *target_module_name_obj, const char *target_type_name);
PyObject *sip_get_qualname(PyTypeObject *scope_py_type, PyObject *name);
PyObject *sip_get_scope_dict(sipSipModuleState *sms, const sipTypeDef *td,
        PyObject *mod_dict, const sipWrappedModuleDef *wmd);
void sip_instance_destroyed(sipWrappedModuleState *wms,
        sipSimpleWrapper **sipSelfp);
int sip_is_subtype(const sipClassTypeDef *ctd,
        const sipClassTypeDef *base_ctd);
int sip_keep_reference(sipWrappedModuleState *wms, sipSimpleWrapper *w,
        int key, PyObject *obj);
PyObject *sip_next_in_mro(PyObject *self, PyObject *after);
void sip_raise_no_convert_from(const sipTypeDef *td);
void sip_remove_from_parent(sipWrapper *self);
int sip_register_py_type(sipSipModuleState *sms, PyTypeObject *supertype);
int sip_super_init(PyObject *self, PyObject *args, PyObject *kwds,
        PyObject *type);
void sip_transfer_back(sipSipModuleState *sms, PyObject *self);
void sip_transfer_to(sipSipModuleState *sms, PyObject *self, PyObject *owner);
sipTypeID sip_type_scope(sipWrappedModuleState *wms, sipTypeID type_id);
PyObject *sip_unpickle_type(PyObject *mod, PyObject *args);
PyObject *sip_wrap_simple_instance(sipSipModuleState *sms, void *cpp,
        PyTypeObject *py_type, sipWrapper *owner, int flags);


#ifdef __cplusplus
}
#endif

#endif
