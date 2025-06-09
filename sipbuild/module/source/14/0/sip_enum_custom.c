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
static int convert_to_enum(sipSipModuleState *sms, PyObject *obj,
        const sipTypeDef *td, int allow_int);
static PyObject *create_scoped_enum(sipSipModuleState *sms,
        sipExportedModuleDef *client, sipEnumTypeDef *etd, int enum_nr,
        PyObject *name);
static PyObject *create_unscoped_enum(sipSipModuleState *sms,
        sipExportedModuleDef *client, sipEnumTypeDef *etd, PyObject *name);
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
    ((sipTypeDef *)(sms->current_enum_backdoor))->td_py_type = (PyTypeObject *)py_type;

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
    sipSipModuleState *sms = sip_get_sip_module_state_from_wrapper_type(
            Py_TYPE(self));
    PyObject *res;
    sipEnumTypeDef *etd;
    sipExportedModuleDef *client;
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
    client = ((sipTypeDef *)etd)->td_module;

    /* Find the number of this enum. */
    for (enum_nr = 0; enum_nr < client->em_nrtypes; ++enum_nr)
        if (client->em_types[enum_nr] == (sipTypeDef *)etd)
            break;

    /* Get the enum members in the same scope. */
    if (etd->etd_scope < 0)
    {
        nr_members = client->em_nrenummembers;
        enm = client->em_enummembers;
    }
    else
    {
        const sipContainerDef *cod = sip_get_container(client->em_types[etd->etd_scope]);

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
            sipPyNameOfEnum(etd), name_str);

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
        const sipTypeDef *td)
{
    return sip_enum_convert_from_enum(sip_get_sip_module_state(wmod), member,
            td);
}


/*
 * Implement the creation of a Python object for a member of a named enum.
 */
PyObject *sip_enum_convert_from_enum(sipSipModuleState *Py_UNUSED(sms),
        int member, const sipTypeDef *td)
{
    assert(sipTypeIsEnum(td) || sipTypeIsScopedEnum(td));

    return PyObject_CallFunction((PyObject *)sipTypeAsPyTypeObject(td), "(i)",
            member);
}


/*
 * Convert a Python object implementing an enum to an integer value.  An
 * exception is raised if there was an error.
 */
int sip_api_convert_to_enum(PyObject *wmod, PyObject *obj,
        const sipTypeDef *td)
{
    return sip_enum_convert_to_enum(sip_get_sip_module_state(wmod), obj, td);
}


/*
 * Implement the conversion of a Python object implementing an enum to an
 * integer value.
 */
int sip_enum_convert_to_enum(sipSipModuleState *sms, PyObject *obj,
        const sipTypeDef *td)
{
    return convert_to_enum(sms, obj, td, TRUE);
}


/*
 * Convert a Python object implementing a constrained enum to an integer value.
 */
int sip_enum_convert_to_constrained_enum(sipSipModuleState *sms, PyObject *obj,
        const sipTypeDef *td)
{
    return convert_to_enum(sms, obj, td, FALSE);
}


/*
 * Create an enum object and add it to a dictionary.  A negative value is
 * returned (and an exception set) if there was an error.
 */
int sip_enum_create_custom_enum(sipSipModuleState *sms,
        sipExportedModuleDef *client, sipEnumTypeDef *etd, int enum_nr,
        PyObject *mod_dict)
{
    int rc;
    PyObject *name, *dict, *enum_obj;

    etd->etd_base.td_module = client;

    /* Get the dictionary into which the type will be placed. */
    if (etd->etd_scope < 0)
        dict = mod_dict;
    else if ((dict = sip_get_scope_dict(sms, client->em_types[etd->etd_scope], mod_dict, client)) == NULL)
        return -1;

    /* Create an object corresponding to the type name. */
    if ((name = PyUnicode_FromString(sipPyNameOfEnum(etd))) == NULL)
        return -1;

    /* Create the enum. */
    if (sipTypeIsEnum(&etd->etd_base))
        enum_obj = create_unscoped_enum(sms, client, etd, name);
    else
        enum_obj = create_scoped_enum(sms, client, etd, enum_nr, name);

    if (enum_obj == NULL)
    {
        Py_DECREF(name);
        return -1;
    }

    /* Add the enum to the "parent" dictionary. */
    rc = PyDict_SetItem(dict, name, enum_obj);

    /* We can now release our remaining references. */
    Py_DECREF(name);
    Py_DECREF(enum_obj);

    return rc;
}


/*
 * Return the generated type structure for a custom enum object that wraps a
 * C/C++ enum or NULL (and no exception set) if the object is something else.
 */
const sipTypeDef *sip_enum_get_generated_type(sipSipModuleState *sms,
        PyTypeObject *py_type)
{
    if (PyObject_TypeCheck((PyObject *)py_type, sms->custom_enum_type))
        return ((sipEnumTypeObject *)py_type)->type;

    return NULL;
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
    PyObject *sip_mod = sip_get_sip_module(defining_class);
    const sipTypeDef *td = ((sipEnumTypeObject *)Py_TYPE(self))->type;

    // TODO Why not save the callable in the module state?
    return Py_BuildValue("N(Osi)",
            PyObject_GetAttrString(sip_mod, "_unpickle_enum"),
            td->td_module->em_nameobj, sipPyNameOfEnum((sipEnumTypeDef *)td),
            (int)PyLong_AS_LONG(self));
}


/*
 * The enum unpickler.
 */
PyObject *sip_enum_unpickle_custom_enum(PyObject *mod, PyObject *args)
{
    sipSipModuleState *sms = (sipSipModuleState *)PyModule_GetState(mod);
    PyObject *mname_obj, *evalue_obj;
    const char *ename;
    sipExportedModuleDef *em;
    int i;

    if (!PyArg_ParseTuple(args, "UsO:_unpickle_enum", &mname_obj, &ename, &evalue_obj))
        return NULL;

    /* Get the module definition. */
    if ((em = sip_get_module(sms, mname_obj)) == NULL)
        return NULL;

    /* Find the enum type object. */
    for (i = 0; i < em->em_nrtypes; ++i)
    {
        sipTypeDef *td = em->em_types[i];

        if (td != NULL && !sipTypeIsStub(td) && sipTypeIsEnum(td))
            if (strcmp(sipPyNameOfEnum((sipEnumTypeDef *)td), ename) == 0)
                return PyObject_CallFunctionObjArgs((PyObject *)sipTypeAsPyTypeObject(td), evalue_obj, NULL);
    }

    PyErr_Format(PyExc_SystemError, "unable to find to find enum: %s", ename);

    return NULL;
}


/*
 * Convert a Python object implementing a named enum (or, optionally, an int)
 * to an integer value.
 */
static int convert_to_enum(sipSipModuleState *sms, PyObject *obj,
        const sipTypeDef *td, int allow_int)
{
    int val;

    assert(sipTypeIsEnum(td) || sipTypeIsScopedEnum(td));

    if (sipTypeIsScopedEnum(td))
    {
        if (PyObject_IsInstance(obj, (PyObject *)sipTypeAsPyTypeObject(td)) <= 0)
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
            if (!PyObject_TypeCheck(obj, sipTypeAsPyTypeObject(td)))
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
        sipExportedModuleDef *client, sipEnumTypeDef *etd, int enum_nr,
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
        nr_members = client->em_nrenummembers;
        enm = client->em_enummembers;
    }
    else
    {
        const sipContainerDef *cod = sip_get_container(client->em_types[etd->etd_scope]);

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

    rc = PyDict_SetItem(kw_args, module_s, client->em_nameobj);
    Py_DECREF(module_s);

    if (rc < 0)
        goto rel_kw_args;

    /*
     * If the enum has a scope then the default __qualname__ will be incorrect.
     */
     // TODO Review this.
     if (etd->etd_scope >= 0)
     {
        PyObject *qualname_arg = PyUnicode_InternFromString("qualname");

        if (qualname_arg == NULL)
            goto rel_kw_args;

        PyObject *qualname;

        if ((qualname = sip_get_qualname(client->em_types[etd->etd_scope], name)) == NULL)
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

    if ((enum_obj = PyObject_Call(sms->enum_int_enum_type, args, kw_args)) == NULL)
        goto rel_kw_args;

    Py_DECREF(kw_args);
    Py_DECREF(args);
    Py_DECREF(members);

    /* Note that it isn't actually a PyTypeObject. */
    etd->etd_base.td_py_type = (PyTypeObject *)enum_obj;

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
        sipExportedModuleDef *client, sipEnumTypeDef *etd, PyObject *name)
{
    static PyObject *bases = NULL;
    PyObject *type_dict, *args;
    sipEnumTypeObject *eto;

    /* Create the base type tuple if it hasn't already been done. */
    if (bases == NULL)
        if ((bases = PyTuple_Pack(1, (PyObject *)&PyLong_Type)) == NULL)
            return NULL;

    /* Create the type dictionary. */
    if ((type_dict = sip_create_type_dict(client)) == NULL)
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
     if (etd->etd_scope >= 0)
     {
        /* Append the name of the enum to the scope's __qualname__. */
        Py_CLEAR(eto->super.ht_qualname);
        eto->super.ht_qualname = sip_get_qualname(
                client->em_types[etd->etd_scope], name);

        if (eto->super.ht_qualname == NULL)
        {
            Py_DECREF((PyObject *)eto);
            return NULL;
        }
    }

    return (PyObject *)eto;
}


/*
 * Raise an exception when failing to convert an enum because of its type.
 */
static void enum_expected(PyObject *obj, const sipTypeDef *td)
{
    PyErr_Format(PyExc_TypeError, "a member of enum '%s' is expected not '%s'",
            sipPyNameOfEnum((sipEnumTypeDef *)td), Py_TYPE(obj)->tp_name);
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
