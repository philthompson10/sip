/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the core sip module internal interfaces.
 *
 * Copyright (c) 2026 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_CORE_H
#define _SIP_CORE_H

#include <Python.h>

#include <stdint.h>

#include "sip.h"
#include "sip_decls.h"


#ifdef __cplusplus
extern "C" {
#endif

#undef  TRUE
#define TRUE        1

#undef  FALSE
#define FALSE       0

#define AUTO_DOCSTRING          '\1'    /* Marks an auto class docstring. */


/* Macros to access the parts of a valid type ID. */
#define sipTypeIDTypeNr(id)             ((sipTypeNr)(id))
#define sipTypeIDModuleNr(id)           ((sipModuleNr)((id) >> 16))
#define sipTypeIDIsSentinel(id)         ((id) & SIP_TYPE_ID_SENTINEL)
#define sipTypeIDIsAbsolute(id)         ((id) & SIP_TYPE_ID_ABSOLUTE)
#define sipTypeIDIsExternal(id)         ((id) & SIP_TYPE_ID_EXTERNAL)
#define sipTypeIDIsPOD(id)              (((id) & SIP_TYPE_ID_TYPE_MASK) == SIP_TYPE_ID_TYPE_POD)
#define sipTypeIDIsClass(id)            (((id) & SIP_TYPE_ID_TYPE_MASK) == SIP_TYPE_ID_TYPE_CLASS)
#define sipTypeIDIsMapped(id)           (((id) & SIP_TYPE_ID_TYPE_MASK) == SIP_TYPE_ID_TYPE_MAPPED)
#define sipTypeIDIsEnum(id)             (((id) & SIP_TYPE_ID_TYPE_MASK) == SIP_TYPE_ID_TYPE_ENUM)
#define sipTypeIDIsCurrentModule(id)    (((id) & SIP_TYPE_ID_CURRENT_MODULE) == SIP_TYPE_ID_CURRENT_MODULE)

/*
 * An entry in the linked list of event handlers.
 */
typedef struct _sipEventHandler {
    const sipTypeSpec *td;          /* The type the handler handles. */
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
 * The SIP ABI implementation.
 */
extern const sipABISpec sip_abi;


/*
 * These are part of the SIP API but are also used within the SIP module.
 */
int sip_api_convert_from_slice_object(PyObject *slice, Py_ssize_t length,
        Py_ssize_t *start, Py_ssize_t *stop, Py_ssize_t *step,
        Py_ssize_t *slicelength);
int sip_api_deprecated(const char *classname, const char *method,
        const char *message);
int sip_api_enable_autoconversion(PyTypeObject *w_type, int enable);
void sip_api_free(void *mem);
void *sip_api_get_address(PyObject *w_inst);
void *sip_api_get_cpp_ptr(PyObject *mod, PyObject *w_inst, sipTypeID type_id);
void *sip_api_malloc(size_t nbytes);


/*
 * These are not part of the SIP API but are used within the SIP module.
 */
void sip_add_to_parent(sipWrapper *self, sipWrapper *owner);
int sip_append_py_object_to_list(PyObject **listp, PyObject *object);
void *sip_cast_cpp_ptr(void *ptr, PyTypeObject *src_type,
        PyTypeObject *target_type);
int sip_check_pointer(void *ptr, PyObject *w_inst);
PyTypeObject *sip_create_mapped_type(sipSipModuleState *sms,
        const sipModuleSpec *wmd, const sipMappedTypeSpec *mtd,
        PyObject *w_mod_dict);
PyObject *sip_create_type_dict(const sipModuleSpec *wmd);
int sip_dict_set_and_discard(PyObject *dict, const char *name, PyObject *obj);
int sip_fix_type_attrs(sipModuleState *ms, const char *fq_py_name,
        PyObject *py_type);
const sipContainerSpec *sip_get_container(const sipTypeSpec *td);
void *sip_get_complex_cpp_ptr(sipModuleState *wms, PyObject *w_inst,
        sipTypeID type_id);
void *sip_get_cpp_ptr(PyObject *w_inst, PyTypeObject *target_type);
void *sip_get_final_address(sipSipModuleState *sms, const sipTypeSpec *ts,
        void *cpp);
sipConvertFromFunc sip_get_from_convertor(PyTypeObject *py_type,
        const sipTypeSpec *td);
int sip_get_local_py_type(sipModuleState *wms, sipTypeNr type_nr,
        PyTypeObject **py_type_p);
const sipTypeSpec *sip_get_type_detail(sipModuleState *wms, sipTypeID type_id,
        PyTypeObject **py_type_p, sipModuleState **defining_wms_p);
PyTypeObject *sip_get_py_type(sipModuleState *ms, sipTypeID type_id);
PyTypeObject *sip_get_py_type_from_name(sipSipModuleState *sms,
        PyObject *target_module_name_obj, const char *target_type_name);
int sip_is_subtype(sipModuleState *wms, const sipClassTypeSpec *ctd,
        const sipClassTypeSpec *base_ctd);
int sip_keep_reference(sipModuleState *wms, PyObject *w_inst, int key,
        PyObject *obj);
PyObject *sip_next_in_mro(PyObject *self, PyObject *after);
void sip_raise_no_convert_from(const sipTypeSpec *td);
void sip_remove_from_parent(sipWrapper *self);
int sip_register_py_type(sipSipModuleState *sms, PyTypeObject *supertype);
int sip_super_init(PyObject *self, PyObject *args, PyObject *kwds,
        PyObject *type);
void sip_transfer_back(PyObject *self);
void sip_transfer_to(sipSipModuleState *sms, PyObject *self, PyObject *owner);
sipTypeID sip_type_scope(sipModuleState *wms, sipTypeID type_id);
PyObject *sip_unpickle_type(PyObject *mod, PyObject *args);
void sip_unwrap_instance(sipModuleState *ms, sipSimpleWrapper *sw);
PyObject *sip_wrap_instance(sipSipModuleState *sms, void *cpp,
        PyTypeObject *py_type, PyObject *args, PyObject *owner, int flags);


/*
 * Return the type definition for a type ID.
 */
static inline const sipTypeSpec *sip_get_type_spec(sipModuleState *ms,
        sipTypeID type_id)
{
    return sip_get_type_detail(ms, type_id, NULL, NULL);
}

#ifdef __cplusplus
}
#endif

#endif
