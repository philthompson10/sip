/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The core sip module code.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <datetime.h>

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include "sip_core.h"

#include "sip_array.h"
#include "sip_container.h"
#include "sip_enum.h"
#include "sip_int_convertors.h"
#include "sip_method_descriptor.h"
#include "sip_module.h"
#include "sip_parsers.h"
#include "sip_string_convertors.h"
#include "sip_threads.h"
#include "sip_variable_descriptor.h"
#include "sip_voidptr.h"
#include "sip_wrapped_module.h"
#include "sip_wrapper.h"
#include "sip_wrapper_type.h"


static void sip_api_bad_length_for_slice(Py_ssize_t seqlen,
        Py_ssize_t slicelen);
static PyObject *sip_api_convert_from_enum(PyObject *wmod, int member,
        sipTypeID type_id);
static Py_ssize_t sip_api_convert_from_sequence_index(Py_ssize_t idx,
        Py_ssize_t len);
static int sip_api_convert_to_enum(PyObject *wmod, PyObject *obj,
        sipTypeID type_id);
static int sip_api_get_state(PyObject *transferObj);
static void sip_api_trace(PyObject *wmod, unsigned mask,const char *fmt,...);
static void sip_api_transfer_back(PyObject *wmod, PyObject *self);
static void sip_api_transfer_to(PyObject *wmod, PyObject *self,
        PyObject *owner);
static void sip_api_abstract_method(const char *classname, const char *method);
static void sip_api_bad_class(const char *classname);
static void *sip_api_get_complex_cpp_ptr(PyObject *wmod, sipSimpleWrapper *sw);
static PyObject *sip_api_is_py_method(PyObject *wmod, sip_gilstate_t *gil,
        char *pymc, sipSimpleWrapper **sipSelfp, const char *cname,
        const char *mname);
static void sip_api_call_hook(const char *hookname);
static void sip_api_raise_unknown_exception(void);
static void sip_api_raise_type_exception(PyObject *wmod, sipTypeID type_id,
        void *ptr);
static int sip_api_add_type_instance(PyObject *wmod, PyObject *dict,
        const char *name, void *cppPtr, sipTypeID type_id);
static void sip_api_bad_operator_arg(PyObject *self, PyObject *arg,
        sipPySlotType st);
static PyObject *sip_api_pyslot_extend(PyObject *wmod, sipPySlotType st,
        const sipTypeDef *td, PyObject *arg0, PyObject *arg1);
static void sip_api_add_delayed_dtor(sipSimpleWrapper *w);
static int sip_api_export_symbol(PyObject *wmod, const char *name, void *sym);
static void *sip_api_import_symbol(PyObject *wmod, const char *name);
static sipTypeID sip_api_find_type_id(PyObject *wmod, const char *type);
static int sip_api_register_py_type(PyObject *wmod, PyTypeObject *supertype);
static const char *sip_api_resolve_typedef(PyObject *wmod, const char *name);
static PyObject *sip_api_get_reference(PyObject *self, int key);
static int sip_api_is_owned_by_python(sipSimpleWrapper *sw);
static int sip_api_is_derived_class(sipSimpleWrapper *sw);
static int sip_api_init_mixin(PyObject *wmod, PyObject *self, PyObject *args,
        PyObject *kwds, sipTypeID type_id);
static void *sip_api_get_mixin_address(sipSimpleWrapper *w,
        const sipTypeDef *td);
static PyInterpreterState *sip_api_get_interpreter(PyObject *wmod);
static void sip_api_set_type_user_data(sipWrapperType *wt, void *data);
static void *sip_api_get_type_user_data(const sipWrapperType *wt);
static PyObject *sip_api_py_type_dict(const PyTypeObject *py_type);
static PyObject *sip_api_py_type_dict_ref(PyTypeObject *py_type);
static const char *sip_api_py_type_name(const PyTypeObject *py_type);
static int sip_api_get_method(PyObject *obj, sipMethodDef *method);
static PyObject *sip_api_from_method(const sipMethodDef *method);
static int sip_api_get_c_function(PyObject *obj, sipCFunctionDef *c_function);
static int sip_api_get_date(PyObject *obj, sipDateDef *date);
static PyObject *sip_api_from_date(const sipDateDef *date);
static int sip_api_get_datetime(PyObject *obj, sipDateDef *date,
        sipTimeDef *time);
static PyObject *sip_api_from_datetime(const sipDateDef *date,
        const sipTimeDef *time);
static int sip_api_get_time(PyObject *obj, sipTimeDef *time);
static PyObject *sip_api_from_time(const sipTimeDef *time);
static int sip_api_is_user_type(const sipWrapperType *wt);
static int sip_api_check_plugin_for_type(const sipTypeDef *td,
        const char *name);
static PyObject *sip_api_unicode_new(Py_ssize_t len, unsigned maxchar,
        int *kind, void **data);
static void sip_api_unicode_write(int kind, void *data, int index,
        unsigned value);
static void *sip_api_unicode_data(PyObject *obj, int *char_size,
        Py_ssize_t *len);
static int sip_api_get_buffer_info(PyObject *obj, sipBufferInfoDef *bi);
static void sip_api_release_buffer_info(sipBufferInfoDef *bi);
static PyObject *sip_api_get_user_object(const sipSimpleWrapper *sw);
static void sip_api_set_user_object(sipSimpleWrapper *sw, PyObject *user);
static int sip_api_enable_gc(int enable);
static void sip_api_print_object(PyObject *o);
static int sip_api_register_event_handler(PyObject *wmod, sipEventType type,
        sipTypeID type_id, void *handler);
static void sip_api_instance_destroyed(PyObject *wmod,
        sipSimpleWrapper **sipSelfp);
static void sip_api_visit_wrappers(PyObject *wmod,
        sipWrapperVisitorFunc visitor, void *closure);
static int sip_api_register_exit_notifier(PyMethodDef *md);
static sipExceptionHandler sip_api_next_exception_handler(PyObject *wmod,
        Py_ssize_t *statep);
static PyFrameObject *sip_api_get_frame(int depth);
static sipTypeID sip_api_type_scope(PyObject *wmod, sipTypeID type_id);
static int sip_api_keep_reference(PyObject *wmod, sipSimpleWrapper *w, int key,
        PyObject *obj);


/*
 * The data structure that represents the SIP API.
 */
const sipAPIDef sip_api = {
    /*
     * The following are part of the public API.
     */
    sip_api_bad_catcher_result,
    sip_api_bad_length_for_slice,
    sip_api_build_result,
    sip_api_call_method,
    sip_api_call_procedure_method,
    sip_api_convert_from_sequence_index,
    sip_api_can_convert_to_type,
    sip_api_convert_to_type,
    sip_api_convert_to_type_us,
    sip_api_force_convert_to_type,
    sip_api_force_convert_to_type_us,
    sip_api_release_type,
    sip_api_release_type_us,
    sip_api_convert_from_type,
    sip_api_convert_from_new_type,
    sip_api_convert_from_enum,
    sip_api_get_state,
    sip_api_free,
    sip_api_get_pyobject,
    sip_api_malloc,
    sip_api_trace,
    sip_api_transfer_back,
    sip_api_transfer_to,
    sip_api_long_as_unsigned_long,
    sip_api_convert_from_void_ptr,
    sip_api_convert_from_const_void_ptr,
    sip_api_convert_from_void_ptr_and_size,
    sip_api_convert_from_const_void_ptr_and_size,
    sip_api_convert_to_void_ptr,
    sip_api_export_symbol,
    sip_api_import_symbol,
    sip_api_find_type_id,
    sip_api_register_py_type,
    sip_api_type_scope,
    sip_api_resolve_typedef,
    sip_api_bad_callable_arg,
    sip_api_get_address,
    sip_api_enable_autoconversion,
    sip_api_get_mixin_address,
    sip_api_convert_from_new_pytype,
    sip_api_convert_to_typed_array,
    sip_api_convert_to_array,
    sip_api_get_interpreter,
    sip_api_set_type_user_data,
    sip_api_get_type_user_data,
    sip_api_py_type_dict,
    sip_api_py_type_name,
    sip_api_get_method,
    sip_api_from_method,
    sip_api_get_c_function,
    sip_api_get_date,
    sip_api_from_date,
    sip_api_get_datetime,
    sip_api_from_datetime,
    sip_api_get_time,
    sip_api_from_time,
    sip_api_is_user_type,
    sip_api_check_plugin_for_type,
    sip_api_unicode_new,
    sip_api_unicode_write,
    sip_api_unicode_data,
    sip_api_get_buffer_info,
    sip_api_release_buffer_info,
    sip_api_get_user_object,
    sip_api_set_user_object,
    sip_api_instance_destroyed,
    sip_api_is_owned_by_python,
    sip_api_enable_gc,
    sip_api_print_object,
    sip_api_register_event_handler,
    sip_api_convert_to_enum,
    sip_api_convert_to_bool,
    sip_api_long_as_char,
    sip_api_long_as_signed_char,
    sip_api_long_as_unsigned_char,
    sip_api_long_as_short,
    sip_api_long_as_unsigned_short,
    sip_api_long_as_int,
    sip_api_long_as_unsigned_int,
    sip_api_long_as_long,
    sip_api_long_as_long_long,
    sip_api_long_as_unsigned_long_long,
    sip_api_convert_from_slice_object,
    sip_api_long_as_size_t,
    sip_api_visit_wrappers,
    sip_api_register_exit_notifier,
#if defined(SIP_CONFIGURATION_PyEnums)
    sip_api_is_enum_flag,
#else
    NULL,
#endif
    sip_api_py_type_dict_ref,
    sip_api_get_frame,
    /*
     * The following are not part of the public API.
     */
    sip_api_parse_args,
    sip_api_parse_kwd_args,
    sip_api_parse_pair,
    sip_api_no_function,
    sip_api_no_method,
    sip_api_abstract_method,
    sip_api_bad_class,
    sip_api_get_cpp_ptr,
    sip_api_get_complex_cpp_ptr,
    sip_api_is_py_method,
    sip_api_call_hook,
    sip_api_end_thread,
    sip_api_raise_unknown_exception,
    sip_api_raise_type_exception,
    sip_api_add_type_instance,
    sip_api_bad_operator_arg,
    sip_api_pyslot_extend,
    sip_api_add_delayed_dtor,
    sip_api_bytes_as_char,
    sip_api_bytes_as_char_array,
    sip_api_bytes_as_string,
    sip_api_string_as_ascii_char,
    sip_api_string_as_ascii_string,
    sip_api_string_as_latin1_char,
    sip_api_string_as_latin1_string,
    sip_api_string_as_utf8_char,
    sip_api_string_as_utf8_string,
    sip_api_string_as_wchar,
    sip_api_string_as_wstring,
    sip_api_deprecated,
    sip_api_keep_reference,
    sip_api_add_exception,
    sip_api_parse_result,
    sip_api_call_error_handler,
    sip_api_init_mixin,
    sip_api_get_reference,
    sip_api_is_derived_class,
    sip_api_next_exception_handler,
    sip_api_wrapped_module_clear,
    sip_api_wrapped_module_free,
    sip_api_wrapped_module_traverse,
    sip_api_simple_wrapper_configure,
    sip_api_simple_wrapper_init,
};


/* Forward references. */
static void call_py_dtor(sipWrappedModuleState *wms, sipSimpleWrapper *self);
static int compare_typedef_name(const void *key, const void *el);
static PyTypeObject *create_class_type(sipWrappedModuleState *wms,
        const sipClassTypeDef *ctd);
static PyTypeObject *create_container_type(sipWrappedModuleState *wms,
        const sipContainerDef *cod, const sipTypeDef *td, PyObject *bases,
        PyTypeObject *metatype);
static PyTypeObject *find_registered_py_type(sipSipModuleState *sms,
        const char *name);
static void *find_slot(PyObject *self, sipPySlotType st);
static void *find_slot_in_class(const sipClassTypeDef *psd, sipPySlotType st);
static void *find_slot_in_slot_list(const sipPySlotDef *psd, sipPySlotType st);
static sipWrappedModuleState *get_defining_wrapped_module_state(
        sipWrappedModuleState *wms, sipTypeID type_id);
static PyObject *import_module_attr(const char *module, const char *attr);
static int objobjargproc_slot(PyObject *self, PyObject *arg1, PyObject *arg2,
        sipPySlotType st);
#if 0
static PyObject *pickle_type(PyObject *self, PyTypeObject *defining_class,
        PyObject *const *args, Py_ssize_t nargs, PyObject *kwd_args);
#endif
#if 0
static int set_reduce(PyTypeObject *type, PyMethodDef *pickler);
#endif
static int ssizeobjargproc_slot(PyObject *self, Py_ssize_t arg1,
        PyObject *arg2, sipPySlotType st);


/*
 * Set a dictionary item and discard the reference to the item even if there
 * was an error.
 */
int sip_dict_set_and_discard(PyObject *dict, const char *name, PyObject *obj)
{
    int rc;

    if (obj == NULL)
        return -1;

    rc = PyDict_SetItemString(dict, name, obj);

    Py_DECREF(obj);

    return rc;
}


/*
 * Return the current interpreter, if there is one.
 */
static PyInterpreterState *sip_api_get_interpreter(PyObject *wmod)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    return wms->sip_module_state->interpreter_state;
}


/*
 * Display a printf() style message to stderr according to the current trace
 * mask.
 */
static void sip_api_trace(PyObject *wmod, unsigned mask, const char *fmt, ...)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    va_list ap;

    va_start(ap,fmt);

    if (wms->sip_module_state->trace_mask & mask)
        vfprintf(stderr, fmt, ap);

    va_end(ap);
}


/*
 * Create a Python object for a member of a named enum.
 */
static PyObject *sip_api_convert_from_enum(PyObject *wmod, int member,
        sipTypeID type_id)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    return sip_enum_convert_from_enum(wms, member, type_id);
}


/*
 * Convert a Python object implementing an enum to an integer value.  An
 * exception is raised if there was an error.
 */
static int sip_api_convert_to_enum(PyObject *wmod, PyObject *obj,
        sipTypeID type_id)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    return sip_enum_convert_to_enum(wms, obj, type_id);
}


/*
 * Register the given Python type.
 */
static int sip_api_register_py_type(PyObject *wmod, PyTypeObject *type)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    return sip_register_py_type(wms->sip_module_state, type);
}


/*
 * Implement the registration of a Python type.
 */
int sip_register_py_type(sipSipModuleState *sms, PyTypeObject *type)
{
    return sip_append_py_object_to_list(&sms->registered_py_types,
            (PyObject *)type);
}


/*
 * Find the registered type with the given name.  Raise an exception if it
 * couldn't be found.
 */
static PyTypeObject *find_registered_py_type(sipSipModuleState *sms,
        const char *name)
{
    if (sms->registered_py_types != NULL)
    {
        Py_ssize_t i;

        for (i = 0; i < PyList_GET_SIZE(sms->registered_py_types); ++i)
        {
            PyTypeObject *type = (PyTypeObject *)PyList_GET_ITEM(
                    sms->registered_py_types, i);

            if (strcmp(type->tp_name, name) == 0)
                return type;
        }
    }

    PyErr_Format(PyExc_RuntimeError, "%s is not a registered type", name);

    return NULL;
}


/*
 * Add a wrapped C/C++ pointer to the list of delayed dtors.
 */
static void sip_api_add_delayed_dtor(sipSimpleWrapper *sw)
{
#if 0
    // TODO
    void *ptr;
    const sipClassTypeDef *ctd;

    if ((ptr = sip_get_ptr_type_def(sw, &ctd)) == NULL)
        return;

    sipSipModuleState *sms = sip_get_sip_module_state_from_sip_type(
            Py_TYPE((PyObject *)sw));

    /* Find the defining module. */
    Py_ssize_t i;

    for (i = 0; i < PyList_GET_SIZE(sms->module_list); i++)
    {
        PyObject *mod = PyList_GET_ITEM(sms->module_list, i);
        sipWrappedModuleState *ms = (sipWrappedModuleState *)PyModule_GetState(
                mod);
        const sipWrappedModuleDef *md = ms->wrapped_module_def;
        int i;

        for (i = 0; i < md->nr_type_defs; ++i)
            if (md->type_defs[i] == (const sipTypeDef *)ctd)
            {
                sipDelayedDtor *dd;

                if ((dd = sip_api_malloc(sizeof (sipDelayedDtor))) == NULL)
                    return;

                /* Add to the list. */
                dd->dd_ptr = ptr;
                dd->dd_name = ctd->ctd_container.cod_name;
                dd->dd_isderived = sipIsDerived(sw);
                dd->dd_next = ms->delayed_dtors_list;

                ms->delayed_dtors_list = dd;

                return;
            }
    }
#endif
}


/*
 * Keep an extra reference to an object (which may be NULL if the object was
 * optional).
 */
static int sip_api_keep_reference(PyObject *wmod, sipSimpleWrapper *w, int key,
        PyObject *obj)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    return sip_keep_reference(wms, w, key, obj);
}


/*
 * Implement the keeping of an extra reference to an object (which may be NULL
 * if the object was optional).
 */
int sip_keep_reference(sipWrappedModuleState *wms, sipSimpleWrapper *w,
        int key, PyObject *obj)
{
    /* Get a pointer to the dict of extra references. */
    PyObject **extra_refsp = (w != NULL ? &w->extra_refs : &wms->extra_refs);

    /* Create the dict if it doesn't already exist. */
    if (*extra_refsp == NULL && (*extra_refsp = PyDict_New()) == NULL)
        return -1;

    PyObject *key_obj = PyLong_FromLong(key);
    if (key_obj == NULL)
        return -1;

    /* Map NULL onto None and we want to replace any existing reference. */
    if (obj == NULL)
        obj = Py_None;

    int rc = PyDict_SetItem(*extra_refsp, key_obj, obj);
    Py_DECREF(key_obj);

    return rc;
}


/*
 * A wrapper around the Python memory allocater that will raise an exception if
 * if the allocation fails.
 */
void *sip_api_malloc(size_t nbytes)
{
    void *mem;

    if ((mem = PyMem_RawMalloc(nbytes)) == NULL)
        PyErr_NoMemory();

    return mem;
}


/*
 * A wrapper around the Python memory de-allocater.
 */
void sip_api_free(void *mem)
{
    PyMem_RawFree(mem);
}


/*
 * Extend a Python slot by looking in other modules to see if there is an
 * extender function that can handle the arguments.
 */
static PyObject *sip_api_pyslot_extend(PyObject *wmod, sipPySlotType st,
        const sipTypeDef *td, PyObject *arg0, PyObject *arg1)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);
    PyObject *module_list = wms->sip_module_state->module_list;

    /* Go through each module. */
    Py_ssize_t i;

    for (i = 0; i < PyList_GET_SIZE(module_list); i++)
    {
        PyObject *mod = PyList_GET_ITEM(module_list, i);

        /* Skip the module that couldn't handle the arguments. */
        if (mod == wmod)
            continue;

        /* Get the table of the module's extenders. */
        const sipPySlotExtenderDef *ex = wms->wrapped_module_def->slot_extend;

        /* Skip if the module doesn't have any extenders. */
        if (ex == NULL)
            continue;

        /* Go through each extender. */
        for (; ex->pse_func != NULL; ++ex)
        {
            PyObject *res;

            /* Skip if not the right slot type. */
            if (ex->pse_type != st)
                continue;

            /* Check against the type if one was given. */
            if (td != NULL && td != sip_get_type_def(wms, ex->pse_class, NULL))
                continue;

            PyErr_Clear();

            res = ((binaryfunc)ex->pse_func)(arg0, arg1);

            if (res != Py_NotImplemented)
                return res;
        }
    }

    /* The arguments couldn't handled anywhere. */
    PyErr_Clear();

    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}


/*
 * Carry out actions common to all dtors.
 */
static void sip_api_instance_destroyed(PyObject *wmod,
        sipSimpleWrapper **sipSelfp)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    sip_instance_destroyed(wms, sipSelfp);
}


/*
 * Implement the actions common to all dtors.
 */
void sip_instance_destroyed(sipWrappedModuleState *wms,
        sipSimpleWrapper **sipSelfp)
{
    sipSipModuleState *sms = wms->sip_module_state;

    /* If there is no interpreter just do the minimum and get out. */
    if (sms->interpreter_state == NULL)
    {
        *sipSelfp = NULL;
        return;
    }

    SIP_BLOCK_THREADS

    sipSimpleWrapper *sipSelf = *sipSelfp;

    if (sipSelf != NULL)
    {
        PyObject *xtype, *xvalue, *xtb;

        /* We may be tidying up after an exception so preserve it. */
        PyErr_Fetch(&xtype, &xvalue, &xtb);
        call_py_dtor(wms, sipSelf);
        PyErr_Restore(xtype, xvalue, xtb);

        sip_om_remove_object(&sms->object_map, sipSelf);

        /*
         * If C/C++ has a reference (and therefore no parent) then remove it.
         * Otherwise remove the object from any parent.
         */
        if (sipCppHasRef(sipSelf))
        {
            sipResetCppHasRef(sipSelf);
            Py_DECREF(sipSelf);
        }
        else if (PyObject_TypeCheck((PyObject *)sipSelf, sms->wrapper_type))
        {
            sip_remove_from_parent((sipWrapper *)sipSelf);
        }

        /*
         * Normally this is done in the generated dealloc function.  However
         * this is only called if the pointer/access function has not been
         * reset (which it has).  It acts as a guard to prevent any further
         * invocations of reimplemented virtuals.
         */
        *sipSelfp = NULL;
    }

    SIP_UNBLOCK_THREADS
}


/*
 * Call self.__dtor__() if it is implemented.
 */
static void call_py_dtor(sipWrappedModuleState *wms, sipSimpleWrapper *self)
{
    sip_gilstate_t sipGILState;
    char pymc = 0;

    PyObject *method = sip_is_py_method(wms, &sipGILState, &pymc, &self, NULL,
            "__dtor__");

    if (method != NULL)
    {
        PyObject *res = PyObject_CallObject(method, NULL);

        Py_DECREF(method);

        /* Discard any result. */
        Py_XDECREF(res);

        /* Handle any error the best we can. */
        if (PyErr_Occurred())
            PyErr_Print();

        SIP_RELEASE_GIL(sipGILState);
    }
}


/*
 * Add a wrapper to it's parent owner.  The wrapper must not currently have a
 * parent and, therefore, no siblings.
 */
void sip_add_to_parent(sipWrapper *self, sipWrapper *owner)
{
    if (owner->first_child != NULL)
    {
        self->sibling_next = owner->first_child;
        owner->first_child->sibling_prev = self;
    }

    owner->first_child = self;
    self->parent = owner;

    /*
     * The owner holds a real reference so that the cyclic garbage collector
     * works properly.
     */
    Py_INCREF((sipSimpleWrapper *)self);
}


/*
 * Remove a wrapper from it's parent if it has one.
 */
void sip_remove_from_parent(sipWrapper *self)
{
    if (self->parent != NULL)
    {
        if (self->parent->first_child == self)
            self->parent->first_child = self->sibling_next;

        if (self->sibling_next != NULL)
            self->sibling_next->sibling_prev = self->sibling_prev;

        if (self->sibling_prev != NULL)
            self->sibling_prev->sibling_next = self->sibling_next;

        self->parent = NULL;
        self->sibling_next = NULL;
        self->sibling_prev = NULL;

        /*
         * We must do this last, after all the pointers are correct, because
         * this is used by the clear slot.
         */
        Py_DECREF((sipSimpleWrapper *)self);
    }
}


/*
 * Convert a sequence index.  Return the index or a negative value if there was
 * an error.
 */
static Py_ssize_t sip_api_convert_from_sequence_index(Py_ssize_t idx,
        Py_ssize_t len)
{
    /* Negative indices start from the other end. */
    if (idx < 0)
        idx = len + idx;

    if (idx < 0 || idx >= len)
    {
        PyErr_Format(PyExc_IndexError, "sequence index out of range");
        return -1;
    }

    return idx;
}


/*
 * Return the dictionary of a type.
 */
PyObject *sip_get_scope_dict(sipSipModuleState *sms, const sipTypeDef *td,
        PyObject *wmod_dict, const sipWrappedModuleDef *wmd)
{
#if 0
    /*
     * Initialise the scoping type if necessary.  It will always be in the
     * same module if it needs doing.
     */
    if (sipTypeIsMapped(td))
    {
        if (sip_create_mapped_type(sms, wmd, (const sipMappedTypeDef *)td, wmod_dict) == NULL)
            return NULL;

        /* Check that the mapped type can act as a container. */
        assert(sipTypeAsPyTypeObject(td) != NULL);
    }
    else
    {
        if (sip_create_class_type(sms, wmd, (const sipClassTypeDef *)td, wmod_dict) < 0)
            return NULL;
    }

    return (sipTypeAsPyTypeObject(td))->tp_dict;
#else
    return NULL;
#endif
}


/*
 * Create a container type and return a strong reference to it.
 */
static PyTypeObject *create_container_type(sipWrappedModuleState *wms,
        const sipContainerDef *cod, const sipTypeDef *td, PyObject *bases,
        PyTypeObject *metatype)
{
    /* Configure the type. */
    // TODO At moment cod_py_slots is only populated for classes.  If it turns
    // out that other containers don't have slots then move to ctd_py_slots
    // and pass in as an extra argument.  Also check if other containers have
    // optional slots - if not then remove the handling of this possibility.
    const PyType_Slot *slots = cod->cod_py_slots;

    if (slots == NULL)
    {
        static PyType_Slot no_slots[] = {{0, NULL}};

        slots = no_slots;
    }

    PyType_Spec spec = {
        .name = cod->cod_name,
        .basicsize = 0,
        // TODO Add flags to support namespaces, abstract classes etc. so that
        // legacy runtime checks can be removed.
        .flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
        .slots = (PyType_Slot *)slots,
    };

    PyObject *py_type = PyType_FromMetaclass(metatype, wms->wrapped_module,
            &spec, bases);

    if (py_type == NULL)
        goto ret_error;

    /* Configure the type. */
    // TODO Are we going to keep wt_type_id?
    ((sipWrapperType *)py_type)->wt_dmod = Py_NewRef(wms->wrapped_module);
    ((sipWrapperType *)py_type)->wt_td = td;

#if 0
    /* Fix __qualname__ if there is a scope. */
    if (scope_td != NULL)
    {
        PyHeapTypeObject *ht;
        PyObject *qualname = sip_get_qualname(scope_td, name);

        if (qualname == NULL)
            goto rel_type;

        ht = (PyHeapTypeObject *)py_type;

        Py_CLEAR(ht->ht_qualname);
        ht->ht_qualname = qualname;
    }
#endif

    /* Add the type to the scope. */
    PyObject *scope;

    if (cod->cod_scope != sipTypeID_Invalid)
    {
        scope = (PyObject *)sip_get_py_type(wms, cod->cod_scope);

        if (scope == NULL)
            goto rel_type;
    }
    else
    {
        scope = wms->wrapped_module;
    }

    if (PyObject_SetAttrString(scope, cod->cod_name, py_type) < 0)
        goto rel_type;

    return (PyTypeObject *)py_type;

    /* Unwind on error. */

rel_type:
    Py_DECREF(py_type);

ret_error:
    return NULL;
}


/*
 * Create a single class type object.
 */
static PyTypeObject *create_class_type(sipWrappedModuleState *wms,
        const sipClassTypeDef *ctd)
{
    sipSipModuleState *sms = wms->sip_module_state;

    PyObject *bases;

    if (ctd->ctd_supers == NULL)
    {
        if (ctd->ctd_supertype == NULL)
        {
            bases = sipTypeIsNamespace(&ctd->ctd_base) ?
                (PyObject *)sms->simple_wrapper_type :
                (PyObject *)sms->wrapper_type;
        }
        else
        {
            bases = (PyObject *)find_registered_py_type(sms,
                    ctd->ctd_supertype);

            // TODO Check it is a sub-type of simple_wrapper_type.

            if (bases == NULL)
                return NULL;
        }

        Py_INCREF(bases);
    }
    else if (sipTypeIDIsSentinel(ctd->ctd_supers[0]))
    {
        /* There is only one super-type. */
        bases = (PyObject *)sip_get_py_type(wms, ctd->ctd_supers[0]);

        if (bases == NULL)
            return NULL;
    }
    else
    {
        const sipTypeID *supers;
        Py_ssize_t nr_supers = 1;

        for (supers = ctd->ctd_supers; !sipTypeIDIsSentinel(*supers); supers++)
            nr_supers++;

        if ((bases = PyTuple_New(nr_supers)) == NULL)
            return NULL;

        Py_ssize_t i;

        for (i = 0; i < nr_supers; i++)
        {
            PyTypeObject *sup_py_type = sip_get_py_type(wms,
                    ctd->ctd_supers[i]);

            if (sup_py_type == NULL)
                goto rel_bases;

            Py_INCREF(sup_py_type);
            PyTuple_SET_ITEM(bases, i, sup_py_type);
        }
    }

    /*
     * Use the explicit meta-type if there is one, otherwise use the meta-type
     * of the first super-type.
     */
    PyTypeObject *metatype;

    if (ctd->ctd_metatype != NULL)
    {
        metatype = find_registered_py_type(sms, ctd->ctd_metatype);

        if (metatype == NULL)
            goto rel_bases;

        // TODO Check it is a sub-type of wrapper_type_type.
    }
    else
    {
        PyObject *first = PyTuple_CheckExact(bases) ? PyTuple_GET_ITEM(bases, 0) : bases;
        metatype = Py_TYPE(first);
    }

#if 0
This should go.
    /* Create the type dictionary and populate it with any non-lazy methods. */
    PyObject *type_dict;

    if ((type_dict = sip_create_type_dict(wmd)) == NULL)
        goto rel_bases;

    if (sipTypeHasNonlazyMethod(&ctd->ctd_base))
    {
        const PyMethodDef *pmd = ctd->ctd_container.cod_methods;

        for (i = 0; i < ctd->ctd_container.cod_nrmethods; ++i)
        {
            if (is_nonlazy_method(pmd) && add_method(sms, type_dict, pmd) < 0)
                goto rel_dict;

            ++pmd;
        }
    }
#endif

    PyTypeObject *py_type = create_container_type(wms, &ctd->ctd_container,
            (const sipTypeDef *)ctd, bases, metatype);

    Py_DECREF(bases);

    if (py_type == NULL)
        return NULL;

#if 0
    if (ctd->ctd_pyslots != NULL)
        sip_fix_slots(py_type, ctd->ctd_pyslots);
#endif

#if 0
    /* Handle the pickle function. */
    if (ctd->ctd_pickle != NULL)
    {
        static PyMethodDef md = {
            "_pickle_type", (PyCFunction)pickle_type, METH_METHOD|METH_FASTCALL|METH_KEYWORDS, NULL
        };

        if (set_reduce(py_type, &md) < 0)
            goto rel_type;
    }
#endif

    return py_type;

    /* Unwind after an error. */

rel_bases:
    Py_DECREF(bases);

    return NULL;
}


/*
 * Create a single mapped type object.
 */
#if 0
PyTypeObject *sip_create_mapped_type(sipSipModuleState *sms,
        const sipWrappedModuleDef *wmd, const sipMappedTypeDef *mtd,
        PyObject *wmod_dict)
{
    PyObject *type_dict;

    /* Create the type dictionary. */
    if ((type_dict = sip_create_type_dict(wmd)) == NULL)
        return NULL;

    PyTypeObject *container = create_container_type(sms, &mtd->mtd_container,
            // TODO Why wrapper_type instead of simple_wrapper_type?
            (const sipTypeDef *)mtd, sms->wrapper_type,
            (PyObject *)sms->wrapper_type_type, wmod_dict, type_dict, wmd);

    Py_DECREF(type_dict);

    return container;
}
#endif


/*
 * Return the wrapper type object given the names of the module and type.
 */
// TODO For an enum this won't return a wrapper object.
// See sip_api_find_type_id().
PyTypeObject *sip_get_py_type_from_name(sipSipModuleState *sms,
        PyObject *target_module_name_obj, const char *target_type_name)
{
    /* Make sure the module is imported. */
    PyObject *target_mod = PyImport_Import(target_module_name_obj);
    if (target_mod == NULL)
        return NULL;

    Py_DECREF(target_mod);

    /* Find the module definition. */
    Py_ssize_t i;

    for (i = 0; i < PyList_GET_SIZE(sms->module_list); i++)
    {
        PyObject *mod = PyList_GET_ITEM(sms->module_list, i);
        PyObject *name = PyModule_GetNameObject(mod);
        int cmp = PyUnicode_Compare(name, target_module_name_obj);
        Py_DECREF(name);

        if (cmp == 0)
        {
            sipWrappedModuleState *wms = ((sipWrappedModuleState *)PyModule_GetState(mod));
            const sipWrappedModuleDef *wmd = wms->wrapped_module_def;
            int p;

            for (p = 0; p < wmd->nr_type_defs; p++)
            {
                PyTypeObject *py_type = wms->py_types[p];

                if (strcmp(py_type->tp_name, target_type_name) == 0)
                    return py_type;
            }

            PyErr_Format(PyExc_SystemError,
                    "unable to find to find type: %U.%s",
                    target_module_name_obj, target_type_name);

            return NULL;
        }
    }

    PyErr_Format(PyExc_SystemError, "unable to find to find module: %U",
            target_module_name_obj);

    return NULL;
}


/*
 * The type unpickler.
 */
PyObject *sip_unpickle_type(PyObject *mod, PyObject *args)
{
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(mod);
    PyObject *mname_obj, *init_args;
    const char *tname;

    if (!PyArg_ParseTuple(args, "UsO!:_unpickle_type", &mname_obj, &tname, &PyTuple_Type, &init_args))
        return NULL;

    /* Get the wrapper type. */
    PyTypeObject *py_type = sip_get_py_type_from_name(sms, mname_obj, tname);
    if (py_type == NULL)
        return NULL;

    /* Check that it is a class. */
    // TODO This may be an invalid cast.
    const sipTypeDef *td = ((sipWrapperType *)py_type)->wt_td;

    if (!sipTypeIsClass(td))
    {
        PyErr_Format(PyExc_SystemError, "%U.%s is not a class", mname_obj,
                tname);
        return NULL;
    }

    return PyObject_CallObject((PyObject *)py_type, init_args);
}


/*
 * The type pickler, ie. the implementation of __reduce__.
 */
#if 0
Not needed yet.
static PyObject *pickle_type(PyObject *self, PyTypeObject *defining_class,
        PyObject *const *Py_UNUSED(args), Py_ssize_t Py_UNUSED(nargs),
        PyObject *Py_UNUSED(kwd_args))
{
#if 0
    PyObject *sip_mod = sip_get_sip_module(defining_class);
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(sip_mod);

    /* Find the type definition and defining module. */
    Py_ssize_t i;

    for (i = 0; i < PyList_GET_SIZE(sms->module_list); i++)
    {
        PyObject *mod = PyList_GET_ITEM(sms->module_list, i);
        const sipWrappedModuleDef *md = ((sipWrappedModuleState *)PyModule_GetState(mod))->wrapped_module_def;

        int ti;

        for (ti = 0; ti < md->nr_type_defs; ti++)
        {
            const sipTypeDef *td = md->types[ti];

            if (td != NULL && sipTypeIsClass(td))
                if (sipTypeAsPyTypeObject(td) == Py_TYPE(self))
                {
                    PyObject *init_args;
                    const sipClassTypeDef *ctd = (const sipClassTypeDef *)td;
                    const char *pyname = ctd->ctd_container.cod_name;

                    /*
                     * Ask the handwritten pickle code for the tuple of
                     * arguments that will recreate the object.
                     */
                    init_args = ctd->ctd_pickle(sip_get_cpp_ptr(wms, (sipSimpleWrapper *)self, 0));

                    if (init_args == NULL)
                        return NULL;

                    if (!PyTuple_Check(init_args))
                    {
                        PyErr_Format(PyExc_TypeError,
                                "%%PickleCode for type %s.%s did not return a tuple",
                                sipNameOfModule(md), pyname);

                        return NULL;
                    }

                    // TODO Why not save the callable in the module state?
                    return Py_BuildValue("N(NsN)",
                            PyObject_GetAttrString(sip_mod, "_unpickle_type"),
                            PyModule_GetNameObject(mod), pyname, init_args);
                }
        }
    }
#endif

    /* We should never get here. */
    PyErr_Format(PyExc_SystemError, "attempt to pickle unknown type '%s'",
            Py_TYPE(self)->tp_name);

    return NULL;
}
#endif


/*
 * Set the __reduce__method for a type.
 */
#if 0
Not needed yet.
static int set_reduce(PyTypeObject *type, PyMethodDef *pickler)
{
    PyObject *descr;
    int rc;

    /* Create the method descripter. */
    if ((descr = PyDescr_NewMethod(type, pickler)) == NULL)
        return -1;

    PyObject *rstr = PyUnicode_InternFromString("__reduce__");

    if (rstr == NULL)
    {
        Py_DECREF(descr);
        return -1;
    }

    /*
     * Save the method.  Note that we don't use PyObject_SetAttr() as we want
     * to bypass any lazy attribute loading (which may not be safe yet).
     */
    rc = PyType_Type.tp_setattro((PyObject *)type, rstr, descr);
    Py_DECREF(rstr);
    Py_DECREF(descr);

    return rc;
}
#endif


/*
 * Create a type dictionary for dynamic type being created in a module.
 */
PyObject *sip_create_type_dict(const sipWrappedModuleDef *wmd)
{
    PyObject *dict;

    /* Create the dictionary. */
    if ((dict = PyDict_New()) == NULL)
        return NULL;

    // TODO Review the need for this.
#if 0
    PyObject *mstr = PyUnicode_InternFromString("__module__");

    if (mstr == NULL)
    {
        Py_DECREF(dict);
        return NULL;
    }

    /* We need to set the module name as an attribute for dynamic types. */
    int rc = PyDict_SetItem(dict, mstr, wmd->nameobj);
    Py_DECREF(mstr);

    if (rc < 0)
    {
        Py_DECREF(dict);
        return NULL;
    }
#endif

    return dict;
}


/*
 * Return the type ID corresponding to the scope of the given type.
 */
static sipTypeID sip_api_type_scope(PyObject *wmod, sipTypeID type_id)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    return sip_type_scope(wms, type_id);
}


/*
 * Implement the returning of the type ID corresponding to the scope of the
 * given type.
 */
sipTypeID sip_type_scope(sipWrappedModuleState *wms, sipTypeID type_id)
{
#if 0
    if (sipTypeIsEnum(td))
    {
        const sipEnumTypeDef *etd = (const sipEnumTypeDef *)td;

        if (etd->etd_scope >= 0)
            return td->td_module->types[etd->etd_scope];
    }
    else
    {
        const sipContainerDef *cod;

        if (sipTypeIsMapped(td))
            cod = &((const sipMappedTypeDef *)td)->mtd_container;
        else
            cod = &((const sipClassTypeDef *)td)->ctd_container;

        if (cod->cod_scope != sipTypeID_Invalid)
            return sip_get_type_def(wms, cod->cod_scope, NULL);
    }
#endif

    return 0;
}


/*
 * Report an abstract method called with an unbound self.
 */
static void sip_api_abstract_method(const char *classname, const char *method)
{
    PyErr_Format(PyExc_TypeError,
            "%s.%s() is abstract and cannot be called as an unbound method",
            classname, method);
}


/*
 * Report a deprecated class or method with an optional message.
 */
int sip_api_deprecated(const char *classname, const char *method,
        const char *message)
{
    const unsigned int bufsize = 100 + ( message ? strlen(message) : 0 );
    char *buf = (char*)sip_api_malloc(bufsize * sizeof(char));
    unsigned int written = 0;

    if (classname == NULL)
        written = PyOS_snprintf(buf, bufsize, "%s() is deprecated", method);
    else if (method == NULL)
        written = PyOS_snprintf(buf, bufsize, "%s constructor is deprecated",
				classname);
    else
        written = PyOS_snprintf(buf, bufsize, "%s.%s() is deprecated", classname,
				method );

    if (message != NULL)
        PyOS_snprintf(buf+written, bufsize-written, ": %s", message);

    const int res = PyErr_WarnEx(PyExc_DeprecationWarning, buf, 1);
    sip_api_free(buf); 
    return res;
}


/*
 * Report a bad operator argument.  Only a small subset of operators need to
 * be handled (those that don't return Py_NotImplemented).
 */
static void sip_api_bad_operator_arg(PyObject *self, PyObject *arg,
        sipPySlotType st)
{
    const char *sn = NULL;

    /* Try and get the text to match a Python exception. */

    switch (st)
    {
    case concat_slot:
    case iconcat_slot:
        PyErr_Format(PyExc_TypeError,
                "cannot concatenate '%s' and '%s' objects",
                Py_TYPE(self)->tp_name, Py_TYPE(arg)->tp_name);
        break;

    case repeat_slot:
        sn = "*";
        break;

    case irepeat_slot:
        sn = "*=";
        break;

    default:
        sn = "unknown";
    }

    if (sn != NULL)
        PyErr_Format(PyExc_TypeError,
                "unsupported operand type(s) for %s: '%s' and '%s'", sn,
                Py_TYPE(self)->tp_name, Py_TYPE(arg)->tp_name);
}


/*
 * Report a sequence length that does not match the length of a slice.
 */
static void sip_api_bad_length_for_slice(Py_ssize_t seqlen,
        Py_ssize_t slicelen)
{
    PyErr_Format(PyExc_ValueError,
            "attempt to assign sequence of size %zd to slice of size %zd",
            seqlen, slicelen);
}


/*
 * Report a Python object that cannot be converted to a particular class.
 */
static void sip_api_bad_class(const char *classname)
{
    PyErr_Format(PyExc_TypeError,
            "cannot convert Python object to an instance of %s", classname);
}


/*
 * Transfer ownership of a class instance to Python from C/C++.
 */
static void sip_api_transfer_back(PyObject *wmod, PyObject *self)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    sip_transfer_back(wms->sip_module_state, self);
}


/*
 * Implement the transfer of ownership of a class instance to Python from
 * C/C++.
 */
void sip_transfer_back(sipSipModuleState *sms, PyObject *self)
{
    if (self == NULL || !PyObject_TypeCheck(self, sms->wrapper_type))
        return;

    sipSimpleWrapper *sw = (sipSimpleWrapper *)self;

    if (sipCppHasRef(sw))
    {
        sipResetCppHasRef(sw);
        Py_DECREF(sw);
    }
    else
    {
        sip_remove_from_parent((sipWrapper *)sw);
    }

    sipSetPyOwned(sw);
}


/*
 * Transfer ownership of a class instance to C/C++ from Python.
 */
static void sip_api_transfer_to(PyObject *wmod, PyObject *self,
        PyObject *owner)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    sip_transfer_to(wms->sip_module_state, self, owner);
}


/*
 * Implement the transfer of ownership of a class instance to C/C++ from
 * Python.
 */
void sip_transfer_to(sipSipModuleState *sms, PyObject *self,
        PyObject *owner)
{
    /*
     * There is a legitimate case where we try to transfer a PyObject that
     * may not be a SIP generated class.  The virtual handler code calls
     * this function to keep the C/C++ instance alive when it gets rid of
     * the Python object returned by the Python method.  A class may have
     * handwritten code that converts a regular Python type - so we can't
     * assume that we can simply cast to sipWrapper.
     */

    if (self == NULL || !PyObject_TypeCheck(self, sms->wrapper_type))
        return;

    sipSimpleWrapper *sw = (sipSimpleWrapper *)self;

    if (owner == NULL)
    {
        /* There is no owner. */

        if (sipCppHasRef(sw))
        {
            sipResetCppHasRef(sw);
        }
        else
        {
            Py_INCREF(sw);
            sip_remove_from_parent((sipWrapper *)sw);
            sipResetPyOwned(sw);
        }

        Py_DECREF(sw);
    }
    else if (owner == Py_None)
    {
        /*
         * The owner is a C++ instance and not a Python object (ie. there is no
         * parent) so there is an explicit extra reference to keep this Python
         * object alive.  Note that there is no way to specify this from a .sip
         * file - it is useful when embedding in C/C++ applications.
         */

        if (!sipCppHasRef(sw))
        {
            Py_INCREF(sw);
            sip_remove_from_parent((sipWrapper *)sw);
            sipResetPyOwned(sw);

            sipSetCppHasRef(sw);
        }
    }
    else if (PyObject_TypeCheck(owner, sms->wrapper_type))
    {
        /*
         * The owner is a Python object (ie. the C++ instance that the Python
         * object wraps).
         */

        if (sipCppHasRef(sw))
        {
            sipResetCppHasRef(sw);
        }
        else
        {
            Py_INCREF(sw);
            sip_remove_from_parent((sipWrapper *)sw);
            sipResetPyOwned(sw);
        }

        sip_add_to_parent((sipWrapper *)sw, (sipWrapper *)owner);

        Py_DECREF(sw);
    }
}


/*
 * Convert a type instance and add it to a module dictionary or a wrapped
 * type's dictionary.
 */
static int sip_api_add_type_instance(PyObject *wmod, PyObject *dict,
        const char *name, void *cppPtr, sipTypeID type_id)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    if (PyObject_TypeCheck(dict, wms->sip_module_state->wrapper_type_type))
        dict = ((PyTypeObject *)dict)->tp_dict;

    return sip_container_add_type_instance(wms, dict, name, cppPtr, type_id,
            0);
}


/*
 * Return a Python reimplementation corresponding to a C/C++ virtual function,
 * if any.  If one was found then the GIL is acquired.
 */
static PyObject *sip_api_is_py_method(PyObject *wmod, sip_gilstate_t *gil,
        char *pymc, sipSimpleWrapper **sipSelfp, const char *cname,
        const char *mname)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    return sip_is_py_method(wms, gil, pymc, sipSelfp, cname, mname);
}


/*
 * Return the address of the C/C++ instance.
 */
void *sip_api_get_address(sipSimpleWrapper *w)
{
    return w->data;
}


/*
 * Get the C/C++ pointer for a complex object.  Note that not casting the C++
 * pointer is a bug.  However this would only ever be called by PyQt3 signal
 * emitter code and PyQt doesn't contain anything that multiply inherits from
 * QObject.
 */
// TODO Either add the casting type ID or remove the call from the API.
static void *sip_api_get_complex_cpp_ptr(PyObject *wmod, sipSimpleWrapper *sw)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    return sip_get_complex_cpp_ptr(wms, sw, 0);
}


/*
 * Get the C/C++ pointer for a complex object and optionally cast it to the
 * required type.
 */
void *sip_get_complex_cpp_ptr(sipWrappedModuleState *wms, sipSimpleWrapper *sw,
        sipTypeID type_id)
{
    if (!sipIsDerived(sw))
    {
        PyErr_SetString(PyExc_RuntimeError,
                "no access to protected functions or signals for objects not created from Python");

        return NULL;
    }

    return sip_get_cpp_ptr(wms, sw, type_id);
}


/*
 * Get the C/C++ pointer from a wrapper and optionally cast it to the required
 * type.
 */
void *sip_api_get_cpp_ptr(PyObject *wmod, sipSimpleWrapper *sw,
        sipTypeID type_id)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    return sip_get_cpp_ptr(wms, sw, type_id);
}


/*
 * Implement the getting of the C/C++ pointer from a wrapper and optionally
 * cast it to the required type.
 */
void *sip_get_cpp_ptr(sipWrappedModuleState *wms, sipSimpleWrapper *sw,
        sipTypeID type_id)
{
    void *ptr = sip_api_get_address(sw);

    if (sip_check_pointer(ptr, sw) < 0)
        return NULL;

    if (type_id != sipTypeID_Invalid)
    {
        const sipTypeDef *td;
        PyTypeObject *py_type = sip_get_py_type_and_type_def(wms, type_id,
                &td);

        if (PyObject_TypeCheck((PyObject *)sw, py_type))
            ptr = sip_cast_cpp_ptr(ptr, Py_TYPE(sw), td);
        else
            ptr = NULL;

        if (ptr == NULL)
            PyErr_Format(PyExc_TypeError, "could not convert '%s' to '%s'",
                    Py_TYPE(sw)->tp_name,
                    ((const sipClassTypeDef *)td)->ctd_container.cod_name);
    }

    return ptr;
}


/*
 * Cast a C/C++ pointer from a source type to a destination type.
 */
void *sip_cast_cpp_ptr(void *ptr, PyTypeObject *src_type,
        const sipTypeDef *dst_type)
{
    sipCastFunc cast = ((const sipClassTypeDef *)((sipWrapperType *)src_type)->wt_td)->ctd_cast;

    /* C structures and base classes don't have cast functions. */
    if (cast != NULL)
        ptr = (*cast)(ptr, dst_type);

    return ptr;
}


/*
 * Check that a pointer is non-NULL.
 */
int sip_check_pointer(void *ptr, sipSimpleWrapper *sw)
{
    if (ptr == NULL)
    {
        PyErr_Format(PyExc_RuntimeError, (sipWasCreated(sw) ?
                        "wrapped C/C++ object of type %s has been deleted" :
                        "super-class __init__() of type %s was never called"),
                Py_TYPE(sw)->tp_name);
        return -1;
    }

    return 0;
}


/*
 * Get an object that has an extra reference.
 */
static PyObject *sip_api_get_reference(PyObject *self, int key)
{
    PyObject *dict, *key_obj, *obj;

    /* Get the extra references dictionary if there is one. */
    if ((dict = ((sipSimpleWrapper *)self)->extra_refs) == NULL)
        return NULL;

    if ((key_obj = PyLong_FromLong(key)) == NULL)
        return NULL;

    obj = PyDict_GetItem(dict, key_obj);
    Py_DECREF(key_obj);
    Py_XINCREF(obj);

    return obj;
}


/*
 * Return TRUE if an object is owned by Python.
 */
static int sip_api_is_owned_by_python(sipSimpleWrapper *sw)
{
    return sipIsPyOwned(sw);
}


/*
 * Return TRUE if the type of a C++ instance is a derived class.
 */
static int sip_api_is_derived_class(sipSimpleWrapper *sw)
{
    return sipIsDerived(sw);
}


/*
 * Get the user defined object from a wrapped object.
 */
static PyObject *sip_api_get_user_object(const sipSimpleWrapper *sw)
{
    return sw->user;
}


/*
 * Set the user defined object in a wrapped object.
 */
static void sip_api_set_user_object(sipSimpleWrapper *sw, PyObject *user)
{
    sw->user = user;
}


/*
 * Implement the normal transfer policy for the result of %ConvertToTypeCode,
 * ie. it is temporary unless it is being transferred from Python.
 */
int sip_api_get_state(PyObject *transferObj)
{
    return (transferObj == NULL || transferObj == Py_None) ? SIP_TEMPORARY : 0;
}


/*
 * The bsearch() helper function for searching a type definitions table.
 */
static int compare_type_def(const void *key, const void *el)
{
    const char *s1 = (const char *)key;
    const char *s2 = (*(const sipTypeDef **)el)->td_cname;
    char ch1, ch2;

    /*
     * Compare while ignoring spaces so that we don't impose a rigorous naming
     * standard.  This only really affects template-based mapped types.
     */
    do
    {
        while ((ch1 = *s1++) == ' ')
            ;

        while ((ch2 = *s2++) == ' ')
            ;

        /* We might be looking for a pointer or a reference. */
        if ((ch1 == '*' || ch1 == '&' || ch1 == '\0') && ch2 == '\0')
            return 0;
    }
    while (ch1 == ch2);

    return (ch1 < ch2 ? -1 : 1);
}


/*
 * Return the type ID for a named type.
 */
static sipTypeID sip_api_find_type_id(PyObject *wmod, const char *type)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);
    PyObject *module_list = wms->sip_module_state->module_list;
    Py_ssize_t i;

    for (i = 0; i < PyList_GET_SIZE(module_list); i++)
    {
        const sipWrappedModuleDef *md = ((sipWrappedModuleState *)PyModule_GetState(PyList_GET_ITEM(module_list, i)))->wrapped_module_def;

        const sipTypeDef *const *tdp = (const sipTypeDef *const *)bsearch(
                (const void *)type, (const void *)md->type_defs,
                md->nr_type_defs, sizeof (const sipTypeDef *),
                compare_type_def);

        if (tdp != NULL)
        {
            /* Determine the type number. */
            Py_ssize_t type_nr = tdp - md->type_defs;

            /* Return an absolute ID of a generated type. */
            // TODO Does it need to be absolute rather than just relative to
            // the given module?
            return SIP_TYPE_ID_GENERATED | SIP_TYPE_ID_ABSOLUTE | (i << 16) | type_nr;
        }
    }

    return 0;
}


/*
 * Call a hook.
 */
static void sip_api_call_hook(const char *hookname)
{
    PyObject *dictofmods, *mod, *dict, *hook, *res;
 
    /* Get the dictionary of modules. */
    if ((dictofmods = PyImport_GetModuleDict()) == NULL)
        return;
 
    /* Get the builtins module. */
    if ((mod = PyDict_GetItemString(dictofmods, "builtins")) == NULL)
        return;
 
    /* Get it's dictionary. */
    if ((dict = PyModule_GetDict(mod)) == NULL)
        return;
 
    /* Get the function hook. */
    if ((hook = PyDict_GetItemString(dict, hookname)) == NULL)
        return;
 
    /* Call the hook and discard any result. */
    res = PyObject_CallObject(hook, NULL);
 
    Py_XDECREF(res);
}


/*
 * Raise an unknown exception.  Make no assumptions about the GIL.
 */
static void sip_api_raise_unknown_exception(void)
{
    SIP_BLOCK_THREADS

    PyErr_SetObject(PyExc_Exception, PyUnicode_InternFromString("unknown"));

    SIP_UNBLOCK_THREADS
}


/*
 * Raise an exception implemented as a type.  Make no assumptions about the
 * GIL.
 */
static void sip_api_raise_type_exception(PyObject *wmod, sipTypeID type_id,
        void *ptr)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    PyTypeObject *py_type = sip_get_py_type(wms, type_id);

    SIP_BLOCK_THREADS

    PyObject *self = sip_wrap_simple_instance(wms->sip_module_state, ptr,
            py_type, NULL, SIP_PY_OWNED);

    PyErr_SetObject((PyObject *)py_type, self);

    Py_XDECREF(self);

    SIP_UNBLOCK_THREADS
}


/*
 * Return the wrapped module state for the defining module of a type ID.
 */
static sipWrappedModuleState *get_defining_wrapped_module_state(
        sipWrappedModuleState *wms, sipTypeID type_id)
{
    // TODO Add support for external IDs where the module number is the index
    // into a table in the wrapped module state that is a cache of type defs.
    // If in the cache then it must remain valid, ie. should include a strong
    // reference to the defining module.
    // If the cached type def is NULL then the number is the index of a table
    // of strings in the wrapped module definition that are the corresponding
    // names of external types.
    // Add support for dynamic IDs where the module number is the index into
    // the list of all modules held in the sip module state.  Dynamic IDs can
    // only be created by sip_api_find_type_id().
    if (type_id == sipTypeID_Invalid)
        return NULL;

    if (sipTypeIDIsCurrentModule(type_id))
        return wms;

    return (sipWrappedModuleState *)PyModule_GetState(
            PyList_GET_ITEM(wms->imported_modules,
                    sipTypeIDModuleNr(type_id)));
}


/*
 * Return the type definition for a type ID.  This can be invalid if the ID
 * refers to an unresolved external type.
 */
const sipTypeDef *sip_get_type_def(sipWrappedModuleState *wms,
        sipTypeID type_id, sipWrappedModuleState **defining_wms_p)
{
    if ((wms = get_defining_wrapped_module_state(wms, type_id)) == NULL)
        return NULL;

    if (defining_wms_p != NULL)
        *defining_wms_p = wms;

    /*
     * Note that we don't go through the Python type object as Python enums
     * would require special handling.
     */
    return wms->wrapped_module_def->type_defs[sipTypeIDTypeNr(type_id)];
}


/*
 * Return a borrowed reference to the Python type object for a type number in
 * the current module, creating it if necessary.
 */
PyTypeObject *sip_get_local_py_type(sipWrappedModuleState *wms,
        Py_ssize_t type_nr)
{
    PyTypeObject *py_type = wms->py_types[type_nr];
    if (py_type != NULL)
        return py_type;

    const sipTypeDef *td = wms->wrapped_module_def->type_defs[type_nr];

    // TODO Handle unresolved external types.
    // TODO Handle enums.
    // TODO Handle mapped types.
    // TODO Handle namespace extenders.
    assert(sipTypeIsClass(td));

    /* Create the type. */
    py_type = create_class_type(wms, (const sipClassTypeDef *)td);
    if (py_type == NULL)
        return NULL;

    wms->py_types[type_nr] = py_type;

    return py_type;
}


/*
 * Return a borrowed reference to the Python type object for a type ID,
 * creating it if necessary.
 */
PyTypeObject *sip_get_py_type(sipWrappedModuleState *wms, sipTypeID type_id)
{
    if ((wms = get_defining_wrapped_module_state(wms, type_id)) == NULL)
        return NULL;

    return sip_get_local_py_type(wms, sipTypeIDTypeNr(type_id));
}


/*
 * Return the Python type object and the type definition (via a pointer) for a
 * type ID.
 */
// TODO Is this needed?
PyTypeObject *sip_get_py_type_and_type_def(sipWrappedModuleState *wms,
        sipTypeID type_id, const sipTypeDef **tdp)
{
    *tdp = NULL;

    // TODO
    return NULL;
}


/*
 * Find a particular slot function for a type.
 */
static void *find_slot(PyObject *self, sipPySlotType st)
{
    sipSipModuleState *sms = sip_get_sip_module_state_from_sip_type(
            Py_TYPE(self));
    void *slot = NULL;
    PyTypeObject *py_type = Py_TYPE(self);

    /* See if it is a wrapper. */
    if (PyObject_TypeCheck((PyObject *)py_type, sms->wrapper_type_type))
    {
        const sipClassTypeDef *ctd;

        ctd = (sipClassTypeDef *)((sipWrapperType *)(py_type))->wt_td;

        slot = find_slot_in_class(ctd, st);
    }
#if defined(SIP_CONFIGURATION_CustomEnums)
    else
    {
        sipEnumTypeDef *etd;

        /* If it is not a wrapper then it must be an enum. */
        assert(PyObject_TypeCheck((PyObject *)py_type, &sipEnumType_Type));

        etd = (sipEnumTypeDef *)((sipEnumTypeObject *)(py_type))->type;

        assert(etd->etd_pyslots != NULL);

        slot = find_slot_in_slot_list(etd->etd_pyslots, st);
    }
#endif

    return slot;
}


/*
 * Find a particular slot function in a class hierarchy.
 */
static void *find_slot_in_class(const sipClassTypeDef *ctd, sipPySlotType st)
{
#if 0
    void *slot;

    if (ctd->ctd_pyslots != NULL)
        slot = find_slot_in_slot_list(ctd->ctd_pyslots, st);
    else
        slot = NULL;

    if (slot == NULL)
    {
        /* Search any super-types. */
        const sipTypeID *supers;

        if ((supers = ctd->ctd_supers) != NULL)
        {
            sipTypeID type_id;

            do
            {
                type_id = *supers++;

                const sipClassTypeDef *sup_ctd = sip_get_generated_class_type_def(
                        type_id, ctd);

                slot = find_slot_in_class(sup_ctd, st);
            }
            while (slot == NULL && !sipTypeIDIsSentinel(type_id));
        }
    }

    return slot;
#else
    return NULL;
#endif
}


/*
 * Find a particular slot function in a particular type.
 */
static void *find_slot_in_slot_list(const sipPySlotDef *psd, sipPySlotType st)
{
    while (psd->psd_func != NULL)
    {
        if (psd->psd_type == st)
            return psd->psd_func;

        ++psd;
    }

    return NULL;
}


/*
 * Handle an objobjargproc slot.
 */
static int objobjargproc_slot(PyObject *self, PyObject *arg1, PyObject *arg2,
        sipPySlotType st)
{
    int (*f)(PyObject *, PyObject *);
    int res;

    f = (int (*)(PyObject *, PyObject *))find_slot(self, st);

    if (f != NULL)
    {
        PyObject *args;

        /*
         * Slot handlers require a single PyObject *.  The second argument is
         * optional.
         */
        if (arg2 == NULL)
        {
            args = arg1;
            Py_INCREF(args);
        }
        else if ((args = PyTuple_Pack(2, arg1, arg2)) == NULL)
        {
            return -1;
        }

        res = f(self, args);
        Py_DECREF(args);
    }
    else
    {
        PyErr_SetNone(PyExc_NotImplementedError);
        res = -1;
    }

    return res;
}


/*
 * Handle an ssizeobjargproc slot.
 */
static int ssizeobjargproc_slot(PyObject *self, Py_ssize_t arg1,
        PyObject *arg2, sipPySlotType st)
{
    int (*f)(PyObject *, PyObject *);
    int res;

    f = (int (*)(PyObject *, PyObject *))find_slot(self, st);

    if (f != NULL)
    {
        PyObject *args;

        /*
         * Slot handlers require a single PyObject *.  The second argument is
         * optional.
         */
        if (arg2 == NULL)
            args = PyLong_FromSsize_t(arg1);
        else
            args = Py_BuildValue("(nO)", arg1, arg2);

        if (args == NULL)
            return -1;

        res = f(self, args);
        Py_DECREF(args);
    }
    else
    {
        PyErr_SetNone(PyExc_NotImplementedError);
        res = -1;
    }

    return res;
}


/*
 * Get the C++ address of a mixin.
 */
static void *sip_api_get_mixin_address(sipSimpleWrapper *w,
        const sipTypeDef *td)
{
    PyObject *mixin;
    void *cpp;

    if ((mixin = PyObject_GetAttrString((PyObject *)w, td->td_cname)) == NULL)
    {
        PyErr_Clear();
        return NULL;
    }

    cpp = sip_api_get_address((sipSimpleWrapper *)mixin);

    Py_DECREF(mixin);

    return cpp;
}


/*
 * Initialise from a mixin.
 */
static int sip_api_init_mixin(PyObject *wmod, PyObject *self, PyObject *args,
        PyObject *kwds, sipTypeID mixin_type_id)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    /* If we are not a mixin to another wrapped class then behave as normal. */
    const sipTypeDef *td;
    PyTypeObject *mixin_wt = sip_get_py_type_and_type_def(wms, mixin_type_id,
            &td);

    assert(sipTypeIsClass(td));

    if (PyType_IsSubtype(Py_TYPE(self), mixin_wt))
        return sip_super_init(self, args, kwds,
                sip_next_in_mro(self, (PyObject *)mixin_wt));

    /*
     * Create the mixin instance.  Retain the positional arguments for the
     * super-class.  Remember that, even though the mixin appears after the
     * main class in the MRO, it appears before sipWrapperType where the main
     * class's arguments are actually parsed.
     */
    sipSipModuleState *sms = wms->sip_module_state;
    PyObject *unused = NULL;
    sms->unused_backdoor = &unused;
    PyObject *mixin = PyObject_Call((PyObject *)mixin_wt, sms->empty_tuple,
            kwds);
    sms->unused_backdoor = NULL;

    if (mixin == NULL)
        goto gc_unused;

    /* Make sure the mixin can find the main instance. */
    ((sipSimpleWrapper *)mixin)->mixin_main = self;
    Py_INCREF(self);

    PyObject *mixin_name = PyUnicode_FromString(td->td_cname);
    if (mixin_name == NULL)
    {
        Py_DECREF(mixin);
        goto gc_unused;
    }

    int rc = PyObject_SetAttr(self, mixin_name, mixin);
    Py_DECREF(mixin);

    if (rc < 0)
        goto gc_mixin_name;

    /* Add the mixin's useful attributes to the main class. */
    PyObject *double_us = PyUnicode_InternFromString("__");

    if (double_us == NULL)
        goto gc_mixin_name;

    Py_ssize_t pos = 0;
    PyObject *key, *value;

    while (PyDict_Next(mixin_wt->tp_dict, &pos, &key, &value))
    {
        /* Don't replace existing values. */
        if (PyDict_Contains(Py_TYPE(self)->tp_dict, key) != 0)
            continue;

        /* Skip values with names that start with double underscore. */
        if (!PyUnicode_Check(key))
            continue;

        /*
         * Despite what the docs say this returns a Py_ssize_t - although the
         * docs are probably right.
         */
        rc = (int)PyUnicode_Tailmatch(key, double_us, 0, 2, -1);

        if (rc < 0)
            goto gc_double_us;

        if (rc > 0)
            continue;

        if (PyObject_IsInstance(value, (PyObject *)sms->method_descr_type))
        {
            if ((value = sipMethodDescr_Copy(sms, value, mixin_name)) == NULL)
                goto gc_double_us;
        }
        else if (PyObject_IsInstance(value, (PyObject *)sms->variable_descr_type))
        {
            if ((value = sipVariableDescr_Copy(sms, value, mixin_name)) == NULL)
                goto gc_double_us;
        }
        else
        {
            Py_INCREF(value);
        }

        rc = PyDict_SetItem(Py_TYPE(self)->tp_dict, key, value);

        Py_DECREF(value);

        if (rc < 0)
            goto gc_double_us;
    }

    Py_DECREF(double_us);
    Py_DECREF(mixin_name);

    /* Call the super-class's __init__ with any remaining arguments. */
    rc = sip_super_init(self, args, unused,
            sip_next_in_mro(self, (PyObject *)mixin_wt));
    Py_XDECREF(unused);

    return rc;

gc_double_us:
    Py_DECREF(double_us);

gc_mixin_name:
    Py_DECREF(mixin_name);

gc_unused:
    Py_XDECREF(unused);

    return -1;
}


/*
 * Return the next in the MRO of an instance after a given type.
 */
PyObject *sip_next_in_mro(PyObject *self, PyObject *after)
{
    Py_ssize_t i;
    PyObject *mro;

    mro = Py_TYPE(self)->tp_mro;
    assert(PyTuple_Check(mro));

    for (i = 0; i < PyTuple_GET_SIZE(mro); ++i)
        if (PyTuple_GET_ITEM(mro, i) == after)
            break;

    /* Assert that we have found ourself and that we are not the last. */
    assert(i + 1 < PyTuple_GET_SIZE(mro));

    return PyTuple_GET_ITEM(mro, i + 1);
}


/*
 * Call the equivalent of super().__init__() of an instance.
 */
int sip_super_init(PyObject *self, PyObject *args, PyObject *kwds,
        PyObject *type)
{
    int i;
    PyObject *dunder_init, *init, *init_args, *init_res;

    if ((dunder_init = PyUnicode_InternFromString("__init__")) == NULL)
        return -1;

    init = PyObject_GetAttr(type, dunder_init);

    Py_DECREF(dunder_init);

    if (init == NULL)
        return -1;

    if ((init_args = PyTuple_New(1 + PyTuple_GET_SIZE(args))) == NULL)
    {
        Py_DECREF(init);
        return -1;
    }

    PyTuple_SET_ITEM(init_args, 0, self);
    Py_INCREF(self);

    for (i = 0; i < PyTuple_GET_SIZE(args); ++i)
    {
        PyObject *arg = PyTuple_GET_ITEM(args, i);

        PyTuple_SET_ITEM(init_args, 1 + i, arg);
        Py_INCREF(arg);
    }

    init_res = PyObject_Call(init, init_args, kwds);
    Py_DECREF(init_args);
    Py_DECREF(init);
    Py_XDECREF(init_res);

    return (init_res != NULL) ? 0 : -1;
}


/*
 * The type call slot.
 */
static PyObject *slot_call(PyObject *self, PyObject *args, PyObject *kw)
{
    PyObject *(*f)(PyObject *, PyObject *, PyObject *);

    f = (PyObject *(*)(PyObject *, PyObject *, PyObject *))find_slot(self, call_slot);

    assert(f != NULL);

    return f(self, args, kw);
}


/*
 * The sequence type item slot.
 */
static PyObject *slot_sq_item(PyObject *self, Py_ssize_t n)
{
    PyObject *(*f)(PyObject *,PyObject *);
    PyObject *arg, *res;

    if ((arg = PyLong_FromSsize_t(n)) == NULL)
        return NULL;

    f = (PyObject *(*)(PyObject *,PyObject *))find_slot(self, getitem_slot);

    assert(f != NULL);

    res = f(self,arg);

    Py_DECREF(arg);

    return res;
}


/*
 * The mapping type assign subscript slot.
 */
static int slot_mp_ass_subscript(PyObject *self, PyObject *key,
        PyObject *value)
{
    return objobjargproc_slot(self, key, value,
            (value != NULL ? setitem_slot : delitem_slot));
}


/*
 * The sequence type assign item slot.
 */
static int slot_sq_ass_item(PyObject *self, Py_ssize_t i, PyObject *o)
{
    return ssizeobjargproc_slot(self, i, o,
            (o != NULL ? setitem_slot : delitem_slot));
}


/*
 * The type rich compare slot.
 */
static PyObject *slot_richcompare(PyObject *self, PyObject *arg, int op)
{
    PyObject *(*f)(PyObject *,PyObject *);
    sipPySlotType st;

    /* Convert the operation to a slot type. */
    switch (op)
    {
    case Py_LT:
        st = lt_slot;
        break;

    case Py_LE:
        st = le_slot;
        break;

    case Py_EQ:
        st = eq_slot;
        break;

    case Py_NE:
        st = ne_slot;
        break;

    case Py_GT:
        st = gt_slot;
        break;

    case Py_GE:
        st = ge_slot;
        break;

    default:
        /* Suppress a compiler warning. */
        st = -1;
    }

    /* It might not exist if not all the above have been implemented. */
    if ((f = (PyObject *(*)(PyObject *,PyObject *))find_slot(self, st)) == NULL)
    {
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }

    return f(self, arg);
}


/*
 * Add the slot handler for each slot present in the type.
 */
void sip_add_type_slots(PyHeapTypeObject *heap_to, const sipPySlotDef *slots)
{
    PyTypeObject *to;
    PyNumberMethods *nb;
    PySequenceMethods *sq;
    PyMappingMethods *mp;
    PyAsyncMethods *am;
    void *f;

    to = &heap_to->ht_type;
    nb = &heap_to->as_number;
    sq = &heap_to->as_sequence;
    mp = &heap_to->as_mapping;
    am = &heap_to->as_async;

    while ((f = slots->psd_func) != NULL)
        switch (slots++->psd_type)
        {
        case str_slot:
            to->tp_str = (reprfunc)f;
            break;

        case int_slot:
            nb->nb_int = (unaryfunc)f;
            break;

        case float_slot:
            nb->nb_float = (unaryfunc)f;
            break;

        case len_slot:
            mp->mp_length = (lenfunc)f;
            sq->sq_length = (lenfunc)f;
            break;

        case contains_slot:
            sq->sq_contains = (objobjproc)f;
            break;

        case add_slot:
            nb->nb_add = (binaryfunc)f;
            break;

        case concat_slot:
            sq->sq_concat = (binaryfunc)f;
            break;

        case sub_slot:
            nb->nb_subtract = (binaryfunc)f;
            break;

        case mul_slot:
            nb->nb_multiply = (binaryfunc)f;
            break;

        case repeat_slot:
            sq->sq_repeat = (ssizeargfunc)f;
            break;

        case div_slot:
            nb->nb_true_divide = (binaryfunc)f;
            break;

        case mod_slot:
            nb->nb_remainder = (binaryfunc)f;
            break;

        case floordiv_slot:
            nb->nb_floor_divide = (binaryfunc)f;
            break;

        case truediv_slot:
            nb->nb_true_divide = (binaryfunc)f;
            break;

        case and_slot:
            nb->nb_and = (binaryfunc)f;
            break;

        case or_slot:
            nb->nb_or = (binaryfunc)f;
            break;

        case xor_slot:
            nb->nb_xor = (binaryfunc)f;
            break;

        case lshift_slot:
            nb->nb_lshift = (binaryfunc)f;
            break;

        case rshift_slot:
            nb->nb_rshift = (binaryfunc)f;
            break;

        case iadd_slot:
            nb->nb_inplace_add = (binaryfunc)f;
            break;

        case iconcat_slot:
            sq->sq_inplace_concat = (binaryfunc)f;
            break;

        case isub_slot:
            nb->nb_inplace_subtract = (binaryfunc)f;
            break;

        case imul_slot:
            nb->nb_inplace_multiply = (binaryfunc)f;
            break;

        case irepeat_slot:
            sq->sq_inplace_repeat = (ssizeargfunc)f;
            break;

        case idiv_slot:
            nb->nb_inplace_true_divide = (binaryfunc)f;
            break;

        case imod_slot:
            nb->nb_inplace_remainder = (binaryfunc)f;
            break;

        case ifloordiv_slot:
            nb->nb_inplace_floor_divide = (binaryfunc)f;
            break;

        case itruediv_slot:
            nb->nb_inplace_true_divide = (binaryfunc)f;
            break;

        case iand_slot:
            nb->nb_inplace_and = (binaryfunc)f;
            break;

        case ior_slot:
            nb->nb_inplace_or = (binaryfunc)f;
            break;

        case ixor_slot:
            nb->nb_inplace_xor = (binaryfunc)f;
            break;

        case ilshift_slot:
            nb->nb_inplace_lshift = (binaryfunc)f;
            break;

        case irshift_slot:
            nb->nb_inplace_rshift = (binaryfunc)f;
            break;

        case invert_slot:
            nb->nb_invert = (unaryfunc)f;
            break;

        case call_slot:
            to->tp_call = slot_call;
            break;

        case getitem_slot:
            mp->mp_subscript = (binaryfunc)f;
            sq->sq_item = slot_sq_item;
            break;

        case setitem_slot:
        case delitem_slot:
            mp->mp_ass_subscript = slot_mp_ass_subscript;
            sq->sq_ass_item = slot_sq_ass_item;
            break;

        case lt_slot:
        case le_slot:
        case eq_slot:
        case ne_slot:
        case gt_slot:
        case ge_slot:
            to->tp_richcompare = slot_richcompare;
            break;

        case bool_slot:
            nb->nb_bool = (inquiry)f;
            break;

        case neg_slot:
            nb->nb_negative = (unaryfunc)f;
            break;

        case repr_slot:
            to->tp_repr = (reprfunc)f;
            break;

        case hash_slot:
            to->tp_hash = (hashfunc)f;
            break;

        case pos_slot:
            nb->nb_positive = (unaryfunc)f;
            break;

        case abs_slot:
            nb->nb_absolute = (unaryfunc)f;
            break;

        case index_slot:
            nb->nb_index = (unaryfunc)f;
            break;

        case iter_slot:
            to->tp_iter = (getiterfunc)f;
            break;

        case next_slot:
            to->tp_iternext = (iternextfunc)f;
            break;

        case setattr_slot:
            to->tp_setattro = (setattrofunc)f;
            break;

        case matmul_slot:
            nb->nb_matrix_multiply = (binaryfunc)f;
            break;

        case imatmul_slot:
            nb->nb_inplace_matrix_multiply = (binaryfunc)f;
            break;

        case await_slot:
            am->am_await = (unaryfunc)f;
            break;

        case aiter_slot:
            am->am_aiter = (unaryfunc)f;
            break;

        case anext_slot:
            am->am_anext = (unaryfunc)f;
            break;

        /* Suppress a compiler warning. */
        default:
            ;
        }
}


/*
 * If the given name is that of a typedef then the corresponding type is
 * returned.
 */
static const char *sip_api_resolve_typedef(PyObject *wmod, const char *name)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);
    PyObject *module_list = wms->sip_module_state->module_list;
    Py_ssize_t i;

    /*
     * Note that if the same name is defined as more than one type (which is
     * possible if more than one completely independent modules are being
     * used) then we might pick the wrong one.
     */
    for (i = 0; i < PyList_GET_SIZE(module_list); i++)
    {
        const sipWrappedModuleDef *md = ((sipWrappedModuleState *)PyModule_GetState(PyList_GET_ITEM(module_list, i)))->wrapped_module_def;

        if (md->nr_typedefs > 0)
        {
            const sipTypedefDef *tdd;

            tdd = (const sipTypedefDef *)bsearch(name, md->typedefs,
                    md->nr_typedefs, sizeof (sipTypedefDef),
                    compare_typedef_name);

            if (tdd != NULL)
                return tdd->tdd_type_name;
        }
    }

    return NULL;
}


/*
 * The bsearch() helper function for searching a sorted typedef table.
 */
static int compare_typedef_name(const void *key, const void *el)
{
    return strcmp((const char *)key, ((const sipTypedefDef *)el)->tdd_name);
}


/*
 * Add a Python type object to a list.  Return 0 if there was no error.
 */
int sip_append_py_object_to_list(PyObject **listp, PyObject *object)
{
    PyObject *list = *listp;

    if (list != NULL)
        return PyList_Append(list, object);

    if ((list = PyList_New(1)) == NULL)
        return -1;

    PyList_SET_ITEM(list, 0, object);
    Py_INCREF(object);

    *listp = list;

    return 0;
}


/*
 * Register a symbol with a name.  A negative value is returned if the name was
 * already registered.
 */
static int sip_api_export_symbol(PyObject *wmod, const char *name, void *sym)
{
    if (sip_api_import_symbol(wmod, name) != NULL)
        return -1;

    sipSymbol *ss;

    if ((ss = sip_api_malloc(sizeof (sipSymbol))) == NULL)
        return -1;

    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);
    sipSipModuleState *sms = wms->sip_module_state;

    ss->name = name;
    ss->symbol = sym;
    ss->next = sms->symbol_list;

    sms->symbol_list = ss;

    return 0;
}


/*
 * Return the symbol registered with the given name.  NULL is returned if the
 * name was not registered.
 */
static void *sip_api_import_symbol(PyObject *wmod, const char *name)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);
    sipSymbol *ss;

    for (ss = wms->sip_module_state->symbol_list; ss != NULL; ss = ss->next)
        if (strcmp(ss->name, name) == 0)
            return ss->symbol;

    return NULL;
}


/*
 * Register an exit notifier with the atexit module.
 */
static int sip_api_register_exit_notifier(PyMethodDef *md)
{
    static PyObject *register_func = NULL;
    PyObject *notifier, *res;

    if (register_func == NULL && (register_func = import_module_attr("atexit", "register")) == NULL)
        return -1;

    if ((notifier = PyCFunction_New(md, NULL)) == NULL)
        return -1;

    res = PyObject_CallFunctionObjArgs(register_func, notifier, NULL);

    Py_DECREF(notifier);

    if (res == NULL)
        return -1;

    Py_DECREF(res);

    return 0;
}


/*
 * Return the function that converts a C++ instance to a Python object.
 */
sipConvertFromFunc sip_get_from_convertor(PyTypeObject *py_type,
        const sipTypeDef *td)
{
    if (sipTypeIsMapped(td))
        return ((const sipMappedTypeDef *)td)->mtd_cfrom;

    assert(sipTypeIsClass(td));

    sipWrapperType *wt = (sipWrapperType *)py_type;

    if (wt->wt_autoconversion_disabled)
        return NULL;

    return ((const sipClassTypeDef *)td)->ctd_cfrom;
}


/*
 * Enable or disable the auto-conversion.  Returns the previous enabled state.
 */
int sip_api_enable_autoconversion(sipWrapperType *wt, int enable)
{
    int was_enabled = !wt->wt_autoconversion_disabled;

    wt->wt_autoconversion_disabled = !enable;

    return was_enabled;
}


/*
 * Python copies the nb_inplace_add slot to the sq_inplace_concat slot and vice
 * versa if either are missing.  This is a bug because they don't have the same
 * API.  We therefore reverse this.
 */
void sip_fix_slots(PyTypeObject *py_type, sipPySlotDef *psd)
{
    while (psd->psd_func != NULL)
    {
        if (psd->psd_type == iadd_slot && py_type->tp_as_sequence != NULL)
            py_type->tp_as_sequence->sq_inplace_concat = NULL;

        if (psd->psd_type == iconcat_slot && py_type->tp_as_number != NULL)
            py_type->tp_as_number->nb_inplace_add = NULL;

        ++psd;
    }
}


/*
 * Convert a new C/C++ pointer to a Python instance.
 */
PyObject *sip_wrap_simple_instance(sipSipModuleState *sms, void *cpp,
        PyTypeObject *py_type, sipWrapper *owner, int flags)
{
    return sip_wrap_instance(sms, cpp, py_type, sms->empty_tuple, owner,
            flags);
}


/*
 * Get the final C/C++ address.  This is called before we wrap a C/C++ instance
 * (either a mapped type, a C struct or a C++ class) to allow the bindings to
 * replace it with another instance (eg. a proxy).  NULL is returned and an
 * exception raised if there was an error.
 */
// TODO Review what should be passed to event handlers, either a wrapped module
// and a relative type ID or an absolute type ID.
void *sip_get_final_address(sipSipModuleState *sms, const sipTypeDef *td,
        void *cpp)
{
    sipEventHandler *eh;

    /* Invoke any event handlers. */
    for (eh = sms->event_handlers[sipEventWrappingInstance]; eh != NULL; eh = eh->next)
    {
        if (eh->td == td)
        {
            sipWrappingInstanceEventHandler handler = (sipWrappingInstanceEventHandler)eh->handler;

            if ((cpp = handler(td, cpp)) == NULL)
                return NULL;
        }
    }

    return cpp;
}


/*
 * Set the user-specific type data.
 */
static void sip_api_set_type_user_data(sipWrapperType *wt, void *data)
{
    wt->wt_user_data = data;
}


/*
 * Get the user-specific type data.
 */
static void *sip_api_get_type_user_data(const sipWrapperType *wt)
{
    return wt->wt_user_data;
}


/*
 * Get a borrowed reference to the dict of a Python type (on behalf of the
 * limited API).  This is deprecated in ABI v13.6 and must not be used with
 * Python v3.12 and later.
 */
// TODO Remove
static PyObject *sip_api_py_type_dict(const PyTypeObject *py_type)
{
    PyErr_WarnEx(PyExc_DeprecationWarning,
            "sipPyTypeDict() is deprecated, the extension module should use "
            "sipPyTypeDictRef() instead",
            1);

    return py_type->tp_dict;
}


/*
 * Get a new reference to the dict of a Python type (on behalf of the limited
 * API).
 */
static PyObject *sip_api_py_type_dict_ref(PyTypeObject *py_type)
{
#if PY_VERSION_HEX >= 0x030c0000
    return PyType_GetDict(py_type);
#else
    PyObject *ref = py_type->tp_dict;
    Py_XINCREF(ref);
    return ref;
#endif
}


/*
 * Get the name of a Python type (on behalf of the limited API).
 */
static const char *sip_api_py_type_name(const PyTypeObject *py_type)
{
    return py_type->tp_name;
}


/*
 * Check an object is a method and return TRUE and its component parts if it
 * is.
 */
static int sip_api_get_method(PyObject *obj, sipMethodDef *method)
{
    if (!PyMethod_Check(obj))
        return FALSE;

    if (method != NULL)
    {
        method->pm_self = PyMethod_GET_SELF(obj);
        method->pm_function = PyMethod_GET_FUNCTION(obj);
    }

    return TRUE;
}


/*
 * Create a method from its component parts.
 */
static PyObject *sip_api_from_method(const sipMethodDef *method)
{
    return PyMethod_New(method->pm_function, method->pm_self);
}


/*
 * Check an object is a C function and return TRUE and its component parts if
 * it is.
 */
static int sip_api_get_c_function(PyObject *obj, sipCFunctionDef *c_function)
{
    if (!PyCFunction_Check(obj))
        return FALSE;

    if (c_function != NULL)
    {
        c_function->cf_function = ((PyCFunctionObject *)obj)->m_ml;
        c_function->cf_self = PyCFunction_GET_SELF(obj);
    }

    return TRUE;
}


/*
 * Check an object is a date and return TRUE and its component parts if it is.
 */
static int sip_api_get_date(PyObject *obj, sipDateDef *date)
{
    if (!PyDateTimeAPI)
        PyDateTime_IMPORT;

    if (!PyDate_Check(obj))
        return FALSE;

    if (date != NULL)
    {
        date->pd_year = PyDateTime_GET_YEAR(obj);
        date->pd_month = PyDateTime_GET_MONTH(obj);
        date->pd_day = PyDateTime_GET_DAY(obj);
    }

    return TRUE;
}


/*
 * Create a date from its component parts.
 */
static PyObject *sip_api_from_date(const sipDateDef *date)
{
    if (!PyDateTimeAPI)
        PyDateTime_IMPORT;

    return PyDate_FromDate(date->pd_year, date->pd_month, date->pd_day);
}


/*
 * Check an object is a datetime and return TRUE and its component parts if it
 * is.
 */
static int sip_api_get_datetime(PyObject *obj, sipDateDef *date,
        sipTimeDef *time)
{
    if (!PyDateTimeAPI)
        PyDateTime_IMPORT;

    if (!PyDateTime_Check(obj))
        return FALSE;

    if (date != NULL)
    {
        date->pd_year = PyDateTime_GET_YEAR(obj);
        date->pd_month = PyDateTime_GET_MONTH(obj);
        date->pd_day = PyDateTime_GET_DAY(obj);
    }

    if (time != NULL)
    {
        time->pt_hour = PyDateTime_DATE_GET_HOUR(obj);
        time->pt_minute = PyDateTime_DATE_GET_MINUTE(obj);
        time->pt_second = PyDateTime_DATE_GET_SECOND(obj);
        time->pt_microsecond = PyDateTime_DATE_GET_MICROSECOND(obj);
    }

    return TRUE;
}


/*
 * Create a datetime from its component parts.
 */
static PyObject *sip_api_from_datetime(const sipDateDef *date,
        const sipTimeDef *time)
{
    if (!PyDateTimeAPI)
        PyDateTime_IMPORT;

    return PyDateTime_FromDateAndTime(date->pd_year, date->pd_month,
            date->pd_day, time->pt_hour, time->pt_minute, time->pt_second,
            time->pt_microsecond);
}


/*
 * Check an object is a time and return TRUE and its component parts if it is.
 */
static int sip_api_get_time(PyObject *obj, sipTimeDef *time)
{
    if (!PyDateTimeAPI)
        PyDateTime_IMPORT;

    if (!PyTime_Check(obj))
        return FALSE;

    if (time != NULL)
    {
        time->pt_hour = PyDateTime_TIME_GET_HOUR(obj);
        time->pt_minute = PyDateTime_TIME_GET_MINUTE(obj);
        time->pt_second = PyDateTime_TIME_GET_SECOND(obj);
        time->pt_microsecond = PyDateTime_TIME_GET_MICROSECOND(obj);
    }

    return TRUE;
}


/*
 * Create a time from its component parts.
 */
static PyObject *sip_api_from_time(const sipTimeDef *time)
{
    if (!PyDateTimeAPI)
        PyDateTime_IMPORT;

    return PyTime_FromTime(time->pt_hour, time->pt_minute, time->pt_second,
            time->pt_microsecond);
}


/*
 * See if a type is user defined.
 */
static int sip_api_is_user_type(const sipWrapperType *wt)
{
    return wt->wt_user_type;
}


/*
 * Check if a type was generated using the given plugin.  Note that, although
 * this is part of the public API it is undocumented on purpose.
 */
static int sip_api_check_plugin_for_type(const sipTypeDef *td,
        const char *name)
{
#if 0
    /*
     * The current thinking on plugins is that SIP v7 will look for a plugin
     * with a name derived from the name as the current module in the same
     * directory as the .sip defining the module (ie. no %Plugin directive).  A
     * module hierachy may have multiple plugins but they must co-operate.  If
     * a plugin generates user data then it should include a void* (and a
     * run-time API) so that other plugins can extend it further.  This
     * approach means that a plugin's user data structure can be opaque.
     */

    sipWrappedModuleDef *em = td->td_module;
    sipImportedModuleDef *im;

    if (strcmp(sipNameOfModule(em), name) == 0)
        return TRUE;

    if ((im = em->imports) == NULL)
        return FALSE;

    while (im->im_name != NULL)
    {
        if (strcmp(im->im_name, name) == 0)
            return TRUE;

        ++im;
    }
#endif

    return FALSE;
}


/*
 * Create a new Unicode object and return the character size and buffer.
 */
static PyObject *sip_api_unicode_new(Py_ssize_t len, unsigned maxchar,
        int *kind, void **data)
{
    PyObject *obj;

    if ((obj = PyUnicode_New(len, maxchar)) != NULL)
    {
        *kind = PyUnicode_KIND(obj);
        *data = PyUnicode_DATA(obj);
    }

    return obj;
}


/*
 * Update a new Unicode object with a new character.
 */
static void sip_api_unicode_write(int kind, void *data, int index,
        unsigned value)
{
    PyUnicode_WRITE(kind, data, index, value);
}


/*
 * Get the address of the contents of a Unicode object, the character size and
 * the length.
 */
static void *sip_api_unicode_data(PyObject *obj, int *char_size,
        Py_ssize_t *len)
{
    void *data;

    /* Assume there will be an error. */
    *char_size = -1;

    if (PyUnicode_READY(obj) < 0)
        return NULL;

    *len = PyUnicode_GET_LENGTH(obj);

    switch (PyUnicode_KIND(obj))
    {
    case PyUnicode_1BYTE_KIND:
        *char_size = 1;
        data = PyUnicode_1BYTE_DATA(obj);
        break;

    case PyUnicode_2BYTE_KIND:
        *char_size = 2;
        data = PyUnicode_2BYTE_DATA(obj);
        break;

    case PyUnicode_4BYTE_KIND:
        *char_size = 4;
        data = PyUnicode_4BYTE_DATA(obj);
        break;

    default:
        data = NULL;
    }

    return data;
}


/*
 * Get the buffer information supplied by an object that supports the buffer
 * protocol.
 */
static int sip_api_get_buffer_info(PyObject *obj, sipBufferInfoDef *bi)
{
    Py_buffer *buffer;

    if (!PyObject_CheckBuffer(obj))
        return 0;

    if (bi == NULL)
        return 1;

    if ((bi->bi_internal = sip_api_malloc(sizeof (Py_buffer))) == NULL)
        return -1;

    buffer = (Py_buffer *)bi->bi_internal;

    if (PyObject_GetBuffer(obj, buffer, PyBUF_SIMPLE) < 0)
        return -1;

    bi->bi_buf = buffer->buf;
    bi->bi_obj = buffer->obj;
    bi->bi_len = buffer->len;
    bi->bi_readonly = buffer->readonly;
    bi->bi_format = buffer->format;

    return 1;
}


/*
 * Release the buffer information obtained from a previous call to
 * sipGetBufferInfo().
 */
static void sip_api_release_buffer_info(sipBufferInfoDef *bi)
{
    if (bi->bi_internal != NULL)
    {
        PyBuffer_Release((Py_buffer *)bi->bi_internal);
        sip_api_free(bi->bi_internal);
        bi->bi_internal = NULL;
    }
}


/*
 * Enable or disable the garbage collector.  Return the previous state or -1 if
 * there was an error.
 */
static int sip_api_enable_gc(int enable)
{
    static PyObject *enable_func = NULL, *disable_func, *isenabled_func;
    PyObject *result;
    int was_enabled;

    /*
     * This may be -ve in the highly unusual event that a previous call failed.
     */
    if (enable < 0)
        return -1;

    /* Get the functions if we haven't already got them. */
    if (enable_func == NULL)
    {
        PyObject *gc_module;

        if ((gc_module = PyImport_ImportModule("gc")) == NULL)
            return -1;

        if ((enable_func = PyObject_GetAttrString(gc_module, "enable")) == NULL)
        {
            Py_DECREF(gc_module);
            return -1;
        }

        if ((disable_func = PyObject_GetAttrString(gc_module, "disable")) == NULL)
        {
            Py_DECREF(enable_func);
            Py_DECREF(gc_module);
            return -1;
        }

        if ((isenabled_func = PyObject_GetAttrString(gc_module, "isenabled")) == NULL)
        {
            Py_DECREF(disable_func);
            Py_DECREF(enable_func);
            Py_DECREF(gc_module);
            return -1;
        }

        Py_DECREF(gc_module);
    }

    /* Get the current state. */
    if ((result = PyObject_CallObject(isenabled_func, NULL)) == NULL)
        return -1;

    was_enabled = PyObject_IsTrue(result);
    Py_DECREF(result);

    if (was_enabled < 0)
        return -1;

    /* See if the state needs changing. */
    if (!was_enabled != !enable)
    {
        /* Enable or disable as required. */
        result = PyObject_CallObject((enable ? enable_func : disable_func),
                NULL);

        Py_XDECREF(result);

        if (result != Py_None)
            return -1;
    }

    return was_enabled;
}


/*
 * A thin wrapper around PyObject_Print() usually used when debugging with the
 * limited API.
 */
static void sip_api_print_object(PyObject *o)
{
    PyObject_Print(o, stdout, 0);
}


/*
 * Register a handler for a particular event.
 */
static int sip_api_register_event_handler(PyObject *wmod, sipEventType type,
        sipTypeID type_id, void *handler)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);
    const sipTypeDef *td = sip_get_type_def(wms, type_id, NULL);

    assert(sipTypeIsClass(td) || sipTypeIsMapped(td));

    sipEventHandler *eh = sip_api_malloc(sizeof (sipEventHandler));
    if (eh == NULL)
        return -1;

    sipSipModuleState *sms = wms->sip_module_state;

    eh->td = td;
    eh->handler = handler;
    eh->next = sms->event_handlers[(int)type];
    sms->event_handlers[(int)type] = eh;

    return 0;
}


/*
 * Returns TRUE if a generated class type is a sub-class of a base generated
 * class type.
 */
// TODO Use the Python type objects?
int sip_is_subtype(sipWrappedModuleState *wms, const sipClassTypeDef *ctd,
        const sipClassTypeDef *base_ctd)
{
    /* Handle the trivial cases. */
    if (ctd == base_ctd)
        return TRUE;

    const sipTypeID *supers;

    if ((supers = ctd->ctd_supers) == NULL)
        return FALSE;

    /* Search the super-types. */
    sipTypeID sup_type_id;

    do
    {
        sup_type_id = *supers++;

        sipWrappedModuleState *defining_wms;
        const sipTypeDef *sup_td = sip_get_type_def(wms, sup_type_id,
                &defining_wms);

        if (sip_is_subtype(defining_wms, (const sipClassTypeDef *)sup_td, base_ctd))
            return TRUE;
    }
    while (!sipTypeIDIsSentinel(sup_type_id));

    return FALSE;
}


/*
 * Return an attribute of an imported module.
 */
static PyObject *import_module_attr(const char *module, const char *attr)
{
    PyObject *mod_obj, *attr_obj;

    if ((mod_obj = PyImport_ImportModule(module)) == NULL)
        return NULL;

    attr_obj = PyObject_GetAttrString(mod_obj, attr);

    Py_DECREF(mod_obj);

    return attr_obj;
}


/*
 * Get the container for a generated type.
 */
const sipContainerDef *sip_get_container(const sipTypeDef *td)
{
    if (sipTypeIsMapped(td))
        return &((const sipMappedTypeDef *)td)->mtd_container;

    return &((const sipClassTypeDef *)td)->ctd_container;
}


/*
 * Get the __qualname__ of an object based on its enclosing scope.
 */
PyObject *sip_get_qualname(PyTypeObject *scope_py_type, PyObject *name)
{
    return PyUnicode_FromFormat("%U.%U",
            ((PyHeapTypeObject *)scope_py_type)->ht_qualname, name);
}


/*
 * Unpack a slice object.
 */
int sip_api_convert_from_slice_object(PyObject *slice, Py_ssize_t length,
        Py_ssize_t *start, Py_ssize_t *stop, Py_ssize_t *step,
        Py_ssize_t *slicelength)
{
    if (PySlice_Unpack(slice, start, stop, step) < 0)
        return -1;

    *slicelength = PySlice_AdjustIndices(length, start, stop, *step);

    return 0;
}


/*
 * Call a visitor function for every wrapped object.
 */
static void sip_api_visit_wrappers(PyObject *wmod,
        sipWrapperVisitorFunc visitor, void *closure)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    sip_om_visit_wrappers(&wms->sip_module_state->object_map, visitor,
            closure);
}


/*
 * Raise an exception when there is no mapped type converter to convert from
 * C/C++ to Python.
 */
void sip_raise_no_convert_from(const sipTypeDef *td)
{
    PyErr_Format(PyExc_TypeError, "%s cannot be converted to a Python object",
            td->td_cname);
}


/*
 * Return the next exception handler.  The order is undefined.
 */
static sipExceptionHandler sip_api_next_exception_handler(PyObject *wmod,
        Py_ssize_t *statep)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);
    PyObject *module_list = wms->sip_module_state->module_list;
    Py_ssize_t i;

    for (i = *statep; i < PyList_GET_SIZE(module_list); i++)
    {
        sipExceptionHandler eh = ((sipWrappedModuleState *)PyModule_GetState(PyList_GET_ITEM(module_list, i)))->wrapped_module_def->exception_handler;

        if (eh != NULL)
        {
            *statep = i;
            return eh;
        }
    }

    return NULL;
}


/*
 * Return a frame from the execution stack.
 */
static PyFrameObject *sip_api_get_frame(int depth)
{
#if defined(PYPY_VERSION)
    /* PyPy only supports a depth of 0. */
    return NULL;
#else
    PyFrameObject *frame = PyEval_GetFrame();

    while (frame != NULL && depth > 0)
    {
        frame = PyFrame_GetBack(frame);

        /* We return a borrowed reference. */
        Py_XDECREF(frame);
        --depth;
    }

    return frame;
#endif
}
