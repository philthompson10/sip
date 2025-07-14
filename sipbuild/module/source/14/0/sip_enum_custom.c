/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file implements the enum support using custom enums.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip.h"

#if defined(SIP_CONFIGURATION_CustomEnums)

#include <assert.h>

#include "sip_enum.h"

#include "sip_core.h"
#include "sip_int_convertors.h"
#include "sip_module.h"


/* Forward declarations of slots. */
static PyObject *EnumType_alloc(PyTypeObject *self, Py_ssize_t nitems);
static void EnumType_dealloc(PyObject *self);
static PyObject *EnumType_getattro(PyObject *self, PyObject *name);
static int EnumType_traverse(PyObject *self, visitproc visit, void *arg);


/*
 * The type data structure.  We inherit everything from the standard Python
 * metatype and the size of the type object created is increased to accomodate
 * the extra information we associate with a named enum type.
 */
static PyType_Slot EnumType_slots[] = {
    {Py_tp_alloc, EnumType_alloc},
    {Py_tp_dealloc, EnumType_dealloc},
    {Py_tp_getattro, EnumType_getattro},
    {Py_tp_traverse, EnumType_traverse},
    {0, NULL}
};

static PyType_Spec EnumType_TypeSpec = {
    .name = _SIP_MODULE_FQ_NAME ".enumtype",
    .basicsize = sizeof (sipEnumTypeObject),
    .flags = Py_TPFLAGS_DEFAULT |
#if defined(Py_TPFLAGS_DISALLOW_INSTANTIATION)
             Py_TPFLAGS_DISALLOW_INSTANTIATION |
#endif
#if defined(Py_TPFLAGS_IMMUTABLETYPE)
             Py_TPFLAGS_IMMUTABLETYPE |
#endif
             Py_TPFLAGS_HAVE_GC,
    .slots = EnumType_slots,
};


/* Forward declarations. */
static int convert_to_enum(sipWrappedModuleState *wms, PyObject *obj,
        sipTypeID type_id, int allow_int);
static PyObject *create_scoped_enum(sipSipModuleState *sms,
        const sipWrappedModuleDef *wmd, const sipEnumTypeDef *etd, int enum_nr,
        PyObject *name);
static PyObject *create_unscoped_enum(sipSipModuleState *sms,
        const sipWrappedModuleDef *wmd, const sipEnumTypeDef *etd,
        PyObject *name);
static void enum_expected(PyObject *obj, const sipTypeDef *td);
static int init_enum_module_types(sipSipModuleState *sms);


/*
 * The enum type's alloc slot.
 */
static PyObject *EnumType_alloc(PyTypeObject *self, Py_ssize_t nitems)
{
    sipSipModuleState *sms = sip_get_sip_module_state_from_wrapper_type(self);
    sipEnumTypeObject *py_type;
    sipPySlotDef *psd;

    // TODO Review if this is necessary with the current TP_FLAGS.
    if (sms->current_enum_backdoor == NULL)
    {
        PyErr_SetString(PyExc_TypeError, "enums cannot be sub-classed");
        return NULL;
    }

    assert(sipTypeIsEnum(sms->current_enum_backdoor));

    /* Call the standard super-metatype alloc. */
    if ((py_type = (sipEnumTypeObject *)PyType_Type.tp_alloc(self, nitems)) == NULL)
        return NULL;

    /*
     * Set the links between the Python type object and the generated type
     * structure.  Strictly speaking this doesn't need to be done here.
     */
    py_type->type = sms->current_enum_backdoor;

    /*
     * Initialise any slots.  This must be done here, after the type is
     * allocated but before PyType_Ready() is called.
     */
    if ((psd = ((sipEnumTypeDef *)sms->current_enum_backdoor)->etd_pyslots) != NULL)
        sip_add_type_slots(&py_type->super, psd);

    return (PyObject *)py_type;
}


/*
 * The enum type's dealloc slot.
 */
static void EnumType_dealloc(PyObject *self)
{
    PyObject_GC_UnTrack(self);
    PyTypeObject *type = Py_TYPE(self);
    type->tp_free(self);
    Py_DECREF(type);
}


/*
 * The enum type's getattro slot.
 */
static PyObject *EnumType_getattro(PyObject *self, PyObject *name)
{
#if 0
    sipSipModuleState *sms = sip_get_sip_module_state_from_wrapper_type(
            Py_TYPE(self));
    PyObject *res;
    sipEnumTypeDef *etd;
    const sipWrappedModuleDef *wmd;
    const sipEnumMemberDef *enm, *emd;
    int enum_nr, nr_members, m;
    const char *name_str;

    /*
     * Try a generic lookup first.  This has the side effect of checking the
     * type of the name object.
     */
    if ((res = PyObject_GenericGetAttr(self, name)) != NULL)
        return res;

    if (!PyErr_ExceptionMatches(PyExc_AttributeError))
        return NULL;

    PyErr_Clear();

    /* Get the member name. */
    if ((name_str = PyUnicode_AsUTF8(name)) == NULL)
        return NULL;

    etd = (sipEnumTypeDef *)((sipEnumTypeObject *)self)->type;
    wmd = ((sipTypeDef *)etd)->td_module;

    /* Find the number of this enum. */
    for (enum_nr = 0; enum_nr < wmd->nr_types; ++enum_nr)
        if (wmd->types[enum_nr] == (sipTypeDef *)etd)
            break;

    /* Get the enum members in the same scope. */
    if (etd->etd_scope < 0)
    {
        nr_members = wmd->nr_enum_members;
        enm = wmd->enum_members;
    }
    else
    {
        const sipContainerDef *cod = sip_get_container(wmd->types[etd->etd_scope]);

        nr_members = cod->cod_nrenummembers;
        enm = cod->cod_enummembers;
    }

    /* Find the enum member. */
    for (emd = enm, m = 0; m < nr_members; ++m, ++emd)
        if (emd->em_enum == enum_nr && strcmp(emd->em_name, name_str) == 0)
            return sip_enum_convert_from_enum(sms, emd->em_val,
                    (sipTypeDef *)etd);

    PyErr_Format(PyExc_AttributeError,
            _SIP_MODULE_FQ_NAME ".enumtype object '%s' has no member '%s'",
            etd->etd_name, name_str);
#endif

    return NULL;
}


/*
 * The enum type's traverse slot.
 */
static int EnumType_traverse(PyObject *self, visitproc visit, void *arg)
{
    Py_VISIT(Py_TYPE(self));

    return 0;
}


/*
 * Create a Python object for a member of a named enum.
 */
PyObject *sip_api_convert_from_enum(PyObject *wmod, int member,
        sipTypeID type_id)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    return sip_enum_convert_from_enum(wms, member, type_id);
}


/*
 * Implement the creation of a Python object for a member of a named enum.
 */
PyObject *sip_enum_convert_from_enum(sipWrappedModuleState *wms, int member,
        sipTypeID type_id)
{
    PyTypeObject *py_type = sip_get_py_type(wms, type_id);

    return PyObject_CallFunction((PyObject *)py_type, "(i)", member);
}


/*
 * Implement the conversion of a Python object implementing an enum to an
 * integer value.
 */
int sip_enum_convert_to_enum(sipWrappedModuleState *wms, PyObject *obj,
        sipTypeID type_id)
{
    return convert_to_enum(wms, obj, type_id, TRUE);
}


/*
 * Convert a Python object implementing a constrained enum to an integer value.
 */
int sip_enum_convert_to_constrained_enum(sipWrappedModuleState *wms,
        PyObject *obj, sipTypeID type_id)
{
    return convert_to_enum(wms, obj, type_id, FALSE);
}


/*
 * Create an enum object and add it to a dictionary.  Return a new reference to
 * the enum object or NULL (and an exception set) if there was an error.
 */
ZZZ
PyTypeObject *sip_enum_create_custom_enum(sipSipModuleState *sms,
        const sipWrappedModuleDef *wmd, const sipEnumTypeDef *etd, int enum_nr,
        PyObject *wmod_dict)
{
    /* Get the dictionary into which the type will be placed. */
    PyObject *dict;

    if (etd->etd_scope < 0)
        dict = wmod_dict;
    else if ((dict = sip_get_scope_dict(sms, wmd->types[etd->etd_scope], wmod_dict, wmd)) == NULL)
        return NULL;

    /* Create an object corresponding to the type name. */
    PyObject *name;

    if ((name = PyUnicode_FromString(etd->etd_name)) == NULL)
        return NULL;

    /* Create the enum. */
    PyObject *enum_obj;

    if (sipTypeIsEnum(&etd->etd_base))
        enum_obj = create_unscoped_enum(sms, wmd, etd, name);
    else
        enum_obj = create_scoped_enum(sms, wmd, etd, enum_nr, name);

    if (enum_obj == NULL)
    {
        Py_DECREF(name);
        return NULL;
    }

    /* Add the enum to the "parent" dictionary. */
    int rc = PyDict_SetItem(dict, name, enum_obj);

    Py_DECREF(name);

    if (rc < 0)
    {
        Py_DECREF(enum_obj);
        return NULL;
    }

    return (PyTypeObject *)enum_obj;
}


/*
 * Initialise the enum support.  A negative value is returned (and an exception
 * set) if there was an error.
 */
int sip_enum_init(PyObject *module, sipSipModuleState *sms)
{
    sms->custom_enum_type = (PyTypeObject *)PyType_FromModuleAndSpec(module,
            &EnumType_TypeSpec, NULL);

    if (sms->custom_enum_type == NULL)
        return -1;

    sms->current_enum_backdoor = NULL;

    return 0;
}


/*
 * Return a non-zero value if an object is a sub-class of enum.Enum.
 */
int sip_enum_is_enum(sipSipModuleState *sms, PyObject *obj)
{
    if (init_enum_module_types(sms) < 0)
        return 0;

    return (PyObject_IsSubclass(obj, sms->enum_enum_type) == 1);
}


/*
 * The enum pickler, ie. the implementation of __reduce__.
 */
PyObject *sip_enum_pickle_custom_enum(PyObject *self,
        PyTypeObject *defining_class, PyObject *const *Py_UNUSED(args),
        Py_ssize_t Py_UNUSED(nargs), PyObject *Py_UNUSED(kwd_args))
{
#if 0
    PyObject *sip_mod = sip_get_sip_module(defining_class);
    const sipTypeDef *td = ((sipEnumTypeObject *)Py_TYPE(self))->type;

    // TODO Why not save the callable in the module state?
    return Py_BuildValue("N(Osi)",
            PyObject_GetAttrString(sip_mod, "_unpickle_enum"),
            td->td_module->nameobj,
            ((const sipEnumTypeDef *)td)->etd_name,
            (int)PyLong_AS_LONG(self));
#else
    return NULL;
#endif
}


/*
 * The enum unpickler.
 */
PyObject *sip_enum_unpickle_custom_enum(PyObject *mod, PyObject *args)
{
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(mod);
    PyObject *mname_obj, *evalue_obj;
    const char *ename;

    if (!PyArg_ParseTuple(args, "UsO:_unpickle_enum", &mname_obj, &ename, &evalue_obj))
        return NULL;

    /* Get the wrapper type. */
    PyTypeObject *py_type = sip_get_py_type_from_name(sms, mname_obj, ename);
    if (py_type == NULL)
        return NULL;

    /* Check that it is an enum. */
    // TODO This is an invalid cast.
    const sipTypeDef *td = ((sipWrapperType *)py_type)->wt_td;

    if (!sipTypeIsEnum(td))
    {
        PyErr_Format(PyExc_SystemError, "%U.%s is not an enum", mname_obj,
                ename);
        return NULL;
    }

    return PyObject_CallFunctionObjArgs(py_type, evalue_obj, NULL);
}


/*
 * Convert a Python object implementing a named enum (or, optionally, an int)
 * to an integer value.
 */
static int convert_to_enum(sipWrappedModuleState *wms, PyObject *obj,
        sipTypeID type_id, int allow_int)
{
    const sipTypeDef *td;
    PyTypeObject *py_type = sip_get_py_type_and_type_def(wms, type_id, &td);

    assert(sipTypeIsEnum(td) || sipTypeIsScopedEnum(td));

    int val;

    if (sipTypeIsScopedEnum(td))
    {
        if (PyObject_IsInstance(obj, (PyObject *)py_type) <= 0)
        {
            enum_expected(obj, td);
            return -1;
        }

        PyObject *value = PyUnicode_InternFromString("value");

        if (value == NULL)
            return -1;

        PyObject *val_obj = PyObject_GetAttr(obj, value);
        Py_DECREF(value);

        if (val_obj == NULL)
            return -1;

        /* This will never overflow. */
        val = sip_api_long_as_int(val_obj);

        Py_DECREF(val_obj);
    }
    else
    {
        if (PyObject_TypeCheck((PyObject *)Py_TYPE(obj), sms->custom_enum_type))
        {
            if (!PyObject_TypeCheck(obj, py_typetd))
            {
                enum_expected(obj, td);
                return -1;
            }

            /* This will never overflow. */
            val = sip_api_long_as_int(obj);
        }
        else if (allow_int && PyLong_Check(obj))
        {
            val = sip_api_long_as_int(obj);
        }
        else
        {
            enum_expected(obj, td);
            return -1;
        }
    }

    return val;
}


/*
 * Create a scoped enum.
 */
static PyObject *create_scoped_enum(sipSipModuleState *sms,
        const sipWrappedModuleDef *wmd, const sipEnumTypeDef *etd, int enum_nr,
        PyObject *name)
{
    int i, nr_members, rc;
    sipEnumMemberDef *enm;
    PyObject *members, *enum_obj, *args, *kw_args;

    /* Get the IntEnum type if we haven't done so already. */
    if (init_enum_module_types(sms) < 0)
        goto ret_err;

    /* Create a dict of the members. */
    if ((members = PyDict_New()) == NULL)
        goto ret_err;

    /*
     * Note that the current structures for defining scoped enums are not ideal
     * as we are re-using the ones used for unscoped enums (which are designed
     * to support lazy implementations).
     */
    if (etd->etd_scope < 0)
    {
        nr_members = wmd->nr_enum_members;
        enm = wmd->enum_members;
    }
    else
    {
        const sipContainerDef *cod = sip_get_container(wmd->types[etd->etd_scope]);

        nr_members = cod->cod_nrenummembers;
        enm = cod->cod_enummembers;
    }

    for (i = 0; i < nr_members; ++i)
    {
        if (enm->em_enum == enum_nr)
        {
            PyObject *val = PyLong_FromLong(enm->em_val);

            if (sip_dict_set_and_discard(members, enm->em_name, val) < 0)
                goto rel_members;
        }

        ++enm;
    }

    if ((args = PyTuple_Pack(2, name, members)) == NULL)
        goto rel_members;

    if ((kw_args = PyDict_New()) == NULL)
        goto rel_args;

    PyObject *module_s = PyUnicode_InternFromString("module");

    if (module_s == NULL)
        goto rel_kw_args;

    rc = PyDict_SetItem(kw_args, module_s, wmd->nameobj);
    Py_DECREF(module_s);

    if (rc < 0)
        goto rel_kw_args;

    /*
     * If the enum has a scope then the default __qualname__ will be incorrect.
     */
     // TODO Review the need for this.
#if 0
     if (etd->etd_scope >= 0)
     {
        PyObject *qualname_arg = PyUnicode_InternFromString("qualname");

        if (qualname_arg == NULL)
            goto rel_kw_args;

        PyObject *qualname;

        if ((qualname = sip_get_qualname(wmd->types[etd->etd_scope], name)) == NULL)
        {
            Py_DECREF(qualname_arg);
            goto rel_kw_args;
        }

        rc = PyDict_SetItem(kw_args, qualname_arg, qualname);
        Py_DECREF(qualname_arg);
        Py_DECREF(qualname);

        if (rc < 0)
            goto rel_kw_args;
    }
#endif

    if ((enum_obj = PyObject_Call(sms->enum_int_enum_type, args, kw_args)) == NULL)
        goto rel_kw_args;

    Py_DECREF(kw_args);
    Py_DECREF(args);
    Py_DECREF(members);

    return enum_obj;

    /* Unwind on errors. */

rel_kw_args:
    Py_DECREF(kw_args);

rel_args:
    Py_DECREF(args);

rel_members:
    Py_DECREF(members);

ret_err:
    return NULL;
}


/*
 * Create an unscoped enum.
 */
static PyObject *create_unscoped_enum(sipSipModuleState *sms,
        const sipWrappedModuleDef *wmd, const sipEnumTypeDef *etd,
        PyObject *name)
{
    static PyObject *bases = NULL;
    PyObject *type_dict, *args;
    sipEnumTypeObject *eto;

    /* Create the base type tuple if it hasn't already been done. */
    if (bases == NULL)
        if ((bases = PyTuple_Pack(1, (PyObject *)&PyLong_Type)) == NULL)
            return NULL;

    /* Create the type dictionary. */
    if ((type_dict = sip_create_type_dict(wmd)) == NULL)
        return NULL;

    /* Create the type by calling the metatype. */
    args = PyTuple_Pack(3, name, bases, type_dict);

    Py_DECREF(type_dict);

    if (args == NULL)
        return NULL;

    /* Pass the type via the back door. */
    assert(sms->current_enum_backdoor == NULL);
    sms->current_enum_backdoor = &etd->etd_base;
    eto = (sipEnumTypeObject *)PyObject_Call((PyObject *)sms->custom_enum_type,
            args, NULL);
    sms->current_enum_backdoor = NULL;

    Py_DECREF(args);

    if (eto == NULL)
        return NULL;

    if (etd->etd_pyslots != NULL)
        sip_fix_slots((PyTypeObject *)eto, etd->etd_pyslots);

    /*
     * If the enum has a scope then the default __qualname__ will be incorrect.
     */
     // TODO Review the need for this.
#if 0
     if (etd->etd_scope >= 0)
     {
        /* Append the name of the enum to the scope's __qualname__. */
        Py_CLEAR(eto->super.ht_qualname);
        eto->super.ht_qualname = sip_get_qualname(
                wmd->types[etd->etd_scope], name);

        if (eto->super.ht_qualname == NULL)
        {
            Py_DECREF((PyObject *)eto);
            return NULL;
        }
    }
#endif

    return (PyObject *)eto;
}


/*
 * Raise an exception when failing to convert an enum because of its type.
 */
static void enum_expected(PyObject *obj, const sipTypeDef *td)
{
    PyErr_Format(PyExc_TypeError, "a member of enum '%s' is expected not '%s'",
            ((const sipEnumTypeDef *)td)->etd_name, Py_TYPE(obj)->tp_name);
}


/*
 * Initialise the required types from the standard library enum module.  Return
 * a negative value and raise an exception if there is an error.  Note that we
 * don't do it in enum_init() so that the behaviour is the same as v12.
 */
static int init_enum_module_types(sipSipModuleState *sms)
{
    PyObject *enum_module;

    /* Check if it has already been done. */
    if (sms->enum_enum_type != NULL)
        return 0;

    /* Get the enum types. */
    if ((enum_module = PyImport_ImportModule("enum")) == NULL)
        return -1;

    sms->enum_enum_type = PyObject_GetAttrString(enum_module, "Enum");
    sms->enum_int_enum_type = PyObject_GetAttrString(enum_module, "IntEnum");

    Py_DECREF(enum_module);

    if (sms->enum_enum_type == NULL || sms->enum_int_enum_type == NULL)
    {
        Py_XDECREF(sms->enum_enum_type);
        Py_XDECREF(sms->enum_int_enum_type);

        return -1;
    }

    return 0;
}

#endif
