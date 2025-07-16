/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The implementation of the generic container support.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <string.h>

#include "sip_container.h"

#include "sip.h"
#include "sip_core.h"
#include "sip_enum.h"
#include "sip_method_descriptor.h"
#include "sip_module.h"
#include "sip_variable_descriptor.h"
#include "sip_voidptr.h"
#include "sip_wrapper_type.h"


/* Forward references. */
static int add_char_instances(PyObject *dict, const sipCharInstanceDef *ci);
static int add_double_instances(PyObject *dict,
        const sipDoubleInstanceDef *di);
static int add_lazy_attrs(sipWrappedModuleState *wms, PyTypeObject *py_type,
        const sipTypeDef *td);
static int add_lazy_container_attrs(sipWrappedModuleState *wms,
        sipWrapperType *wt, const sipTypeDef *td, sipContainerDef *cod);
static int add_long_instances(PyObject *dict, const sipLongInstanceDef *li);
static int add_long_long_instances(PyObject *dict,
        const sipLongLongInstanceDef *lli);
static int add_method(sipSipModuleState *sms, PyObject *dict,
        const PyMethodDef *pmd);
static int add_string_instances(PyObject *dict,
        const sipStringInstanceDef *si);
static int add_type_instances(sipWrappedModuleState *wms, PyObject *dict,
        const sipTypeInstanceDef *ti);
static int add_unsigned_long_instances(PyObject *dict,
        const sipUnsignedLongInstanceDef *uli);
static int add_unsigned_long_long_instances(PyObject *dict,
        const sipUnsignedLongLongInstanceDef *ulli);
static int add_void_ptr_instances(sipSipModuleState *sms, PyObject *dict,
        const sipVoidPtrInstanceDef *vi);
static PyObject *create_function(const PyMethodDef *ml);
static PyObject *create_property(const sipVariableDef *vd);
static int is_nonlazy_method(const PyMethodDef *pmd);


/*
 * Add a set of static instances to a dictionary.
 */
int sip_container_add_instances(sipWrappedModuleState *wms, PyObject *dict,
        const sipInstancesDef *id)
{
    if (id->id_type != NULL && add_type_instances(wms, dict, id->id_type) < 0)
        return -1;

    if (id->id_voidp != NULL && add_void_ptr_instances(wms->sip_module_state, dict, id->id_voidp) < 0)
        return -1;

    if (id->id_char != NULL && add_char_instances(dict, id->id_char) < 0)
        return -1;

    if (id->id_string != NULL && add_string_instances(dict, id->id_string) < 0)
        return -1;

#if defined(SIP_CONFIGURATION_CustomEnums)
    if (id->id_int != NULL && sip_container_add_int_instances(dict, id->id_int) < 0)
        return -1;
#endif

    if (id->id_long != NULL && add_long_instances(dict, id->id_long) < 0)
        return -1;

    if (id->id_ulong != NULL && add_unsigned_long_instances(dict, id->id_ulong) < 0)
        return -1;

    if (id->id_llong != NULL && add_long_long_instances(dict, id->id_llong) < 0)
        return -1;

    if (id->id_ullong != NULL && add_unsigned_long_long_instances(dict, id->id_ullong) < 0)
        return -1;

    if (id->id_double != NULL && add_double_instances(dict, id->id_double) < 0)
        return -1;

    return 0;
}


/*
 * Add the int instances to a dictionary.
 */
int sip_container_add_int_instances(PyObject *dict,
        const sipIntInstanceDef *ii)
{
    while (ii->ii_name != NULL)
    {
        PyObject *w = PyLong_FromLong(ii->ii_val);

        if (sip_dict_set_and_discard(dict, ii->ii_name, w) < 0)
            return -1;

        ++ii;
    }

    return 0;
}


/*
 * Populate the type dictionary and all its super-types.
 */
int sip_container_add_lazy_attrs(sipWrappedModuleState *wms,
        PyTypeObject *py_type, const sipTypeDef *td)
{
    if (td == NULL)
        return 0;

    if (add_lazy_attrs(wms, py_type, td) < 0)
        return -1;

    if (sipTypeIsClass(td))
    {
        sipClassTypeDef *ctd = (sipClassTypeDef *)td;
        const sipTypeID *supers;

        if ((supers = ctd->ctd_supers) != NULL)
        {
            sipTypeID type_id;

            do
            {
                type_id = *supers++;

                const sipTypeDef *sup_td;
                PyTypeObject *sup_py_type = sip_get_py_type_and_type_def(wms,
                        type_id, &sup_td);

                if (sip_container_add_lazy_attrs(wms, sup_py_type, sup_td) < 0)
                    return -1;
            }
            while (!sipTypeIDIsSentinel(type_id));
        }
    }

    return 0;
}


/*
 * Wrap a single type instance and add it to a dictionary.
 */
int sip_container_add_type_instance(sipWrappedModuleState *wms, PyObject *dict,
        const char *name, void *cppPtr, sipTypeID type_id, int initflags)
{
    sipSipModuleState *sms = wms->sip_module_state;
    const sipTypeDef *td;
    PyTypeObject *py_type = sip_get_py_type_and_type_def(wms, type_id, &td);
    PyObject *obj;

    if (sipTypeIsEnum(td))
    {
        obj = sip_enum_convert_from_enum(wms, *(int *)cppPtr, type_id);
    }
    else
    {
        sipConvertFromFunc cfrom;

        if ((cppPtr = sip_get_final_address(sms, td, cppPtr)) == NULL)
            return -1;

        cfrom = sip_get_from_convertor(py_type, td);

        if (cfrom != NULL)
        {
            obj = cfrom(cppPtr, NULL);
        }
        else if (sipTypeIsMapped(td))
        {
            sip_raise_no_convert_from(td);
            return -1;
        }
        else
        {
            obj = sip_wrap_simple_instance(sms, cppPtr, py_type, NULL,
                    initflags);
        }
    }

    return sip_dict_set_and_discard(dict, name, obj);
}


/*
 * Add the char instances to a dictionary.
 */
static int add_char_instances(PyObject *dict, const sipCharInstanceDef *ci)
{
    while (ci->ci_name != NULL)
    {
        PyObject *w;

        switch (ci->ci_encoding)
        {
        case 'A':
            w = PyUnicode_DecodeASCII(&ci->ci_val, 1, NULL);
            break;

        case 'L':
            w = PyUnicode_DecodeLatin1(&ci->ci_val, 1, NULL);
            break;

        case '8':
            w = PyUnicode_FromStringAndSize(&ci->ci_val, 1);
            break;

        default:
            w = PyBytes_FromStringAndSize(&ci->ci_val, 1);
        }

        if (sip_dict_set_and_discard(dict, ci->ci_name, w) < 0)
            return -1;

        ++ci;
    }

    return 0;
}


/*
 * Add the double instances to a dictionary.
 */
static int add_double_instances(PyObject *dict, const sipDoubleInstanceDef *di)
{
    while (di->di_name != NULL)
    {
        PyObject *w = PyFloat_FromDouble(di->di_val);

        if (sip_dict_set_and_discard(dict, di->di_name, w) < 0)
            return -1;

        ++di;
    }

    return 0;
}


/*
 * Populate a type dictionary with all lazy attributes if it hasn't already
 * been done.
 */
static int add_lazy_attrs(sipWrappedModuleState *wms, PyTypeObject *py_type,
        const sipTypeDef *td)
{
    sipWrapperType *wt = (sipWrapperType *)py_type;

    /* Handle the trivial case. */
    if (wt->wt_dict_complete)
        return 0;

    if (sipTypeIsMapped(td))
    {
        if (add_lazy_container_attrs(wms, wt, td, &((sipMappedTypeDef *)td)->mtd_container) < 0)
            return -1;
    }
    else
    {
        sipClassTypeDef *nsx;

        /* Search the possible linked list of namespace extenders. */
        for (nsx = (sipClassTypeDef *)td; nsx != NULL; nsx = nsx->ctd_nsextender)
            if (add_lazy_container_attrs(wms, wt, (sipTypeDef *)nsx, &nsx->ctd_container) < 0)
                return -1;
    }

    /*
     * Allow handlers to update the type dictionary.  This must be done last to
     * allow any existing attributes to be replaced.
     */
    sipEventHandler *eh;

    for (eh = wms->sip_module_state->event_handlers[sipEventFinalisingType]; eh != NULL; eh = eh->next)
    {
        if (sipTypeIsClass(eh->td) && sip_is_subtype((const sipClassTypeDef *)td, (const sipClassTypeDef *)eh->td))
        {
            sipFinalisingTypeEventHandler handler = (sipFinalisingTypeEventHandler)eh->handler;

            if (handler(td, ((PyTypeObject *)wt)->tp_dict) < 0)
                return -1;
        }
    }

    wt->wt_dict_complete = TRUE;

    PyType_Modified((PyTypeObject *)wt);

    return 0;
}


/*
 * Populate a container's type dictionary.
 */
static int add_lazy_container_attrs(sipWrappedModuleState *wms,
        sipWrapperType *wt, const sipTypeDef *td, sipContainerDef *cod)
{
    sipSipModuleState *sms = wms->sip_module_state;
    PyObject *dict = ((PyTypeObject *)wt)->tp_dict;
    int i;

    /* Do the methods. */
    const PyMethodDef *pmd;

    for (pmd = cod->cod_methods, i = 0; i < cod->cod_nrmethods; ++i, ++pmd)
    {
        /* Non-lazy methods will already have been handled. */
        if (!sipTypeHasNonlazyMethod(td) || !is_nonlazy_method(pmd))
        {
            if (add_method(sms, dict, pmd) < 0)
                return -1;
        }
    }

#if defined(SIP_CONFIGURATION_PyEnums)
#if 0
    /* Do the Python enums. */
    const sipIntInstanceDef *next_int = cod->cod_instances.id_int;

    if (next_int != NULL)
    {
        sipWrappedModuleDef *module = td->td_module;

        /*
         * Not ideal but we have to look through all types looking for enums
         * for which this container is the enclosing scope.
         */
        for (i = 0; i < module->nr_types; ++i)
        {
            sipTypeDef *enum_td = module->types[i];

            if (enum_td != NULL && sipTypeIsEnum(enum_td))
            {
                sipEnumTypeDef *etd = (sipEnumTypeDef *)enum_td;

                if (module->types[etd->etd_scope] == td)
                    if (sip_enum_create_py_enum(wms, etd, &next_int, dict) < 0)
                        return -1;
            }
        }

        /* Do any remaining ints. */
        if (sip_container_add_int_instances(dict, next_int) < 0)
            return -1;
    }
#endif
#endif

#if defined(SIP_CONFIGURATION_CustomEnums)
    /* Do the unscoped custom enum members. */
    const sipEnumMemberDef *enm;

    for (enm = cod->cod_enummembers, i = 0; i < cod->cod_nrenummembers; ++i, ++enm)
    {
#if 0
        PyObject *val;

        if (enm->em_enum < 0)
        {
            /* It's an unnamed unscoped enum. */
            val = PyLong_FromLong(enm->em_val);
        }
        else
        {
            const sipTypeDef *etd = td->td_module->types[enm->em_enum];

            if (sipTypeIsScopedEnum(etd))
                continue;

            val = sip_enum_convert_from_enum(sms, enm->em_val, etd);
        }

        if (sip_dict_set_and_discard(dict, enm->em_name, val) < 0)
            return -1;
#endif
    }
#endif

    /* Do the variables. */
    const sipVariableDef *vd;

    for (vd = cod->cod_variables, i = 0; i < cod->cod_nrvariables; ++i, ++vd)
    {
        PyObject *descr;

        if (vd->vd_type == PropertyVariable)
            descr = create_property(vd);
        else
            descr = sipVariableDescr_New(sms, vd, td, cod->cod_name);

        if (sip_dict_set_and_discard(dict, vd->vd_name, descr) < 0)
            return -1;
    }

    return 0;
}


/*
 * Add the long instances to a dictionary.
 */
static int add_long_instances(PyObject *dict, const sipLongInstanceDef *li)
{
    while (li->li_name != NULL)
    {
        PyObject *w = PyLong_FromLong(li->li_val);

        if (sip_dict_set_and_discard(dict, li->li_name, w) < 0)
            return -1;

        ++li;
    }

    return 0;
}


/*
 * Add the long long instances to a dictionary.
 */
static int add_long_long_instances(PyObject *dict,
        const sipLongLongInstanceDef *lli)
{
    while (lli->lli_name != NULL)
    {
        PyObject *w = PyLong_FromLongLong(lli->lli_val);

        if (sip_dict_set_and_discard(dict, lli->lli_name, w) < 0)
            return -1;

        ++lli;
    }

    return 0;
}


/*
 * Add a method to a dictionary.
 */
static int add_method(sipSipModuleState *sms, PyObject *dict,
        const PyMethodDef *pmd)
{
    PyObject *descr = sipMethodDescr_New(sms, pmd);

    return sip_dict_set_and_discard(dict, pmd->ml_name, descr);
}


/*
 * Add the string instances to a dictionary.
 */
static int add_string_instances(PyObject *dict, const sipStringInstanceDef *si)
{
    while (si->si_name != NULL)
    {
        PyObject *w;

        switch (si->si_encoding)
        {
        case 'A':
            w = PyUnicode_DecodeASCII(si->si_val, strlen(si->si_val), NULL);
            break;

        case 'L':
            w = PyUnicode_DecodeLatin1(si->si_val, strlen(si->si_val), NULL);
            break;

        case '8':
            w = PyUnicode_FromString(si->si_val);
            break;

        case 'w':
            /* The hack for wchar_t. */
            w = PyUnicode_FromWideChar((const wchar_t *)si->si_val, 1);
            break;

        case 'W':
            /* The hack for wchar_t*. */
            w = PyUnicode_FromWideChar((const wchar_t *)si->si_val,
                    wcslen((const wchar_t *)si->si_val));
            break;

        default:
            w = PyBytes_FromString(si->si_val);
        }

        if (sip_dict_set_and_discard(dict, si->si_name, w) < 0)
            return -1;

        ++si;
    }

    return 0;
}


/*
 * Wrap a set of type instances and add them to a dictionary.
 */
static int add_type_instances(sipWrappedModuleState *wms, PyObject *dict,
        const sipTypeInstanceDef *ti)
{
    while (ti->ti_name != NULL)
    {
        if (sip_container_add_type_instance(wms, dict, ti->ti_name, ti->ti_ptr, ti->ti_type_id, ti->ti_flags) < 0)
            return -1;

        ++ti;
    }

    return 0;
}


/*
 * Add the unsigned long instances to a dictionary.
 */
static int add_unsigned_long_instances(PyObject *dict,
        const sipUnsignedLongInstanceDef *uli)
{
    while (uli->uli_name != NULL)
    {
        PyObject *w = PyLong_FromUnsignedLong(uli->uli_val);

        if (sip_dict_set_and_discard(dict, uli->uli_name, w) < 0)
            return -1;

        ++uli;
    }

    return 0;
}


/*
 * Add the unsigned long long instances to a dictionary.
 */
static int add_unsigned_long_long_instances(PyObject *dict,
        const sipUnsignedLongLongInstanceDef *ulli)
{
    while (ulli->ulli_name != NULL)
    {
        PyObject *w = PyLong_FromUnsignedLongLong(ulli->ulli_val);

        if (sip_dict_set_and_discard(dict, ulli->ulli_name, w) < 0)
            return -1;

        ++ulli;
    }

    return 0;
}


/*
 * Add the void pointer instances to a dictionary.
 */
static int add_void_ptr_instances(sipSipModuleState *sms, PyObject *dict,
        const sipVoidPtrInstanceDef *vi)
{
    while (vi->vi_name != NULL)
    {
        PyObject *w = sip_convert_from_void_ptr(sms, vi->vi_val);

        if (sip_dict_set_and_discard(dict, vi->vi_name, w) < 0)
            return -1;

        ++vi;
    }

    return 0;
}


/*
 * Return a PyCFunction as an object or Py_None if there isn't one.
 */
static PyObject *create_function(const PyMethodDef *ml)
{
    if (ml != NULL)
        return PyCFunction_New((PyMethodDef *)ml, NULL);

    Py_INCREF(Py_None);
    return Py_None;
}


/*
 * Create a Python property object from the SIP generated structure.
 */
static PyObject *create_property(const sipVariableDef *vd)
{
    PyObject *descr, *fget, *fset, *fdel, *doc;

    descr = fget = fset = fdel = doc = NULL;

    if ((fget = create_function(vd->vd_getter)) == NULL)
        goto done;

    if ((fset = create_function(vd->vd_setter)) == NULL)
        goto done;

    if ((fdel = create_function(vd->vd_deleter)) == NULL)
        goto done;

    if (vd->vd_docstring == NULL)
    {
        doc = Py_None;
        Py_INCREF(doc);
    }
    else if ((doc = PyUnicode_FromString(vd->vd_docstring)) == NULL)
    {
        goto done;
    }

    descr = PyObject_CallFunctionObjArgs((PyObject *)&PyProperty_Type, fget,
            fset, fdel, doc, NULL);

done:
    Py_XDECREF(fget);
    Py_XDECREF(fset);
    Py_XDECREF(fdel);
    Py_XDECREF(doc);

    return descr;
}


/*
 * Return non-zero if a method is non-lazy, ie. it must be added to the type
 * when it is created.
 */
static int is_nonlazy_method(const PyMethodDef *pmd)
{
    static const char *lazy[] = {
        "__getattribute__",
        "__getattr__",
        "__enter__",
        "__exit__",
        "__aenter__",
        "__aexit__",
        NULL
    };

    const char **l;

    for (l = lazy; *l != NULL; ++l)
        if (strcmp(pmd->ml_name, *l) == 0)
            return TRUE;

    return FALSE;
}
