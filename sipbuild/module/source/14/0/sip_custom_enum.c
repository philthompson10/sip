/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file implements the enum support using custom enums.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <assert.h>

#include "sip_core.h"

#include "sip_enum.h"


#if defined(SIP_CONFIGURATION_CustomEnums)

static PyObject *enum_type = NULL;              /* The enum.Enum type. */
static PyObject *int_enum_type = NULL;          /* The enum.IntEnum type. */


/* Forward references. */
static int convert_to_enum(PyObject *obj, const sipTypeDef *td, int allow_int);
static PyObject *create_scoped_enum(sipExportedModuleDef *client,
        sipEnumTypeDef *etd, int enum_nr, PyObject *name);
static PyObject *create_unscoped_enum(sipExportedModuleDef *client,
        sipEnumTypeDef *etd, PyObject *name);
static void enum_expected(PyObject *obj, const sipTypeDef *td);
static int init_enum_module_types();
static PyObject *sipEnumType_alloc(PyTypeObject *self, Py_ssize_t nitems);
static PyObject *sipEnumType_getattro(PyObject *self, PyObject *name);


/* The enum unpickler Python function. */
PyObject *sip_enum_custom_enum_unpickler = NULL;

/* The custom enum in the process of being created. */
static sipTypeDef *currentEnum = NULL;


/*
 * The type data structure.  We inherit everything from the standard Python
 * metatype and the size of the type object created is increased to accomodate
 * the extra information we associate with a named enum type.
 */
static PyTypeObject sipEnumType_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    _SIP_MODULE_FQ_NAME ".enumtype",    /* tp_name */
    sizeof (sipEnumTypeObject), /* tp_basicsize */
    0,                      /* tp_itemsize */
    0,                      /* tp_dealloc */
    0,                      /* tp_print */
    0,                      /* tp_getattr */
    0,                      /* tp_setattr */
    0,                      /* tp_as_async (Python v3.5), tp_compare (Python v2) */
    0,                      /* tp_repr */
    0,                      /* tp_as_number */
    0,                      /* tp_as_sequence */
    0,                      /* tp_as_mapping */
    0,                      /* tp_hash */
    0,                      /* tp_call */
    0,                      /* tp_str */
    sipEnumType_getattro,   /* tp_getattro */
    0,                      /* tp_setattro */
    0,                      /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,   /* tp_flags */
    0,                      /* tp_doc */
    0,                      /* tp_traverse */
    0,                      /* tp_clear */
    0,                      /* tp_richcompare */
    0,                      /* tp_weaklistoffset */
    0,                      /* tp_iter */
    0,                      /* tp_iternext */
    0,                      /* tp_methods */
    0,                      /* tp_members */
    0,                      /* tp_getset */
    0,                      /* tp_base */
    0,                      /* tp_dict */
    0,                      /* tp_descr_get */
    0,                      /* tp_descr_set */
    0,                      /* tp_dictoffset */
    0,                      /* tp_init */
    sipEnumType_alloc,      /* tp_alloc */
    0,                      /* tp_new */
    0,                      /* tp_free */
    0,                      /* tp_is_gc */
    0,                      /* tp_bases */
    0,                      /* tp_mro */
    0,                      /* tp_cache */
    0,                      /* tp_subclasses */
    0,                      /* tp_weaklist */
    0,                      /* tp_del */
    0,                      /* tp_version_tag */
    0,                      /* tp_finalize */
    0,                      /* tp_vectorcall */
};


/*
 * Create a Python object for a member of a named enum.
 */
PyObject *sip_api_convert_from_enum(int member, const sipTypeDef *td)
{
    assert(sipTypeIsEnum(td) || sipTypeIsScopedEnum(td));

    return PyObject_CallFunction((PyObject *)sipTypeAsPyTypeObject(td), "(i)",
            member);
}


/*
 * Convert a Python object implementing an enum to an integer value.  An
 * exception is raised if there was an error.
 */
int sip_api_convert_to_enum(PyObject *obj, const sipTypeDef *td)
{
    return convert_to_enum(obj, td, TRUE);
}


/*
 * Convert a Python object implementing a constrained enum to an integer value.
 */
int sip_enum_convert_to_constrained_enum(PyObject *obj, const sipTypeDef *td)
{
    return convert_to_enum(obj, td, FALSE);
}


/*
 * Create an enum object and add it to a dictionary.  A negative value is
 * returned (and an exception set) if there was an error.
 */
int sip_enum_create_custom_enum(sipExportedModuleDef *client,
        sipEnumTypeDef *etd, int enum_nr, PyObject *mod_dict)
{
    int rc;
    PyObject *name, *dict, *enum_obj;

    etd->etd_base.td_module = client;

    /* Get the dictionary into which the type will be placed. */
    if (etd->etd_scope < 0)
        dict = mod_dict;
    else if ((dict = sip_get_scope_dict(client->em_types[etd->etd_scope], mod_dict, client)) == NULL)
        return -1;

    /* Create an object corresponding to the type name. */
    if ((name = PyUnicode_FromString(sipPyNameOfEnum(etd))) == NULL)
        return -1;

    /* Create the enum. */
    if (sipTypeIsEnum(&etd->etd_base))
        enum_obj = create_unscoped_enum(client, etd, name);
    else
        enum_obj = create_scoped_enum(client, etd, enum_nr, name);

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
const sipTypeDef *sip_enum_get_generated_type(PyTypeObject *py_type)
{
    if (PyObject_TypeCheck((PyObject *)py_type, &sipEnumType_Type))
        return ((sipEnumTypeObject *)py_type)->type;

    return NULL;
}


/*
 * Initialise the enum support.  A negative value is returned (and an exception
 * set) if there was an error.
 */
int sip_enum_init(void)
{
    sipEnumType_Type.tp_base = &PyType_Type;

    return PyType_Ready(&sipEnumType_Type);
}


/*
 * Return a non-zero value if an object is a sub-class of enum.Enum.
 */
int sip_enum_is_enum(PyObject *obj)
{
    if (init_enum_module_types() < 0)
        return 0;

    return (PyObject_IsSubclass(obj, enum_type) == 1);
}


/*
 * The enum pickler.
 */
PyObject *sip_enum_pickle_custom_enum(PyObject *obj, PyObject *args)
{
    sipTypeDef *td = ((sipEnumTypeObject *)Py_TYPE(obj))->type;

    (void)args;

    return Py_BuildValue("O(Osi)", sip_enum_custom_enum_unpickler,
            td->td_module->em_nameobj, sipPyNameOfEnum((sipEnumTypeDef *)td),
            (int)PyLong_AS_LONG(obj));
}


/*
 * The enum unpickler.
 */
PyObject *sip_enum_unpickle_custom_enum(PyObject *obj, PyObject *args)
{
    PyObject *mname_obj, *evalue_obj;
    const char *ename;
    sipExportedModuleDef *em;
    int i;

    (void)obj;

    if (!PyArg_ParseTuple(args, "UsO:_unpickle_enum", &mname_obj, &ename, &evalue_obj))
        return NULL;

    /* Get the module definition. */
    if ((em = sip_get_module(mname_obj)) == NULL)
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
static int convert_to_enum(PyObject *obj, const sipTypeDef *td, int allow_int)
{
    int val;

    assert(sipTypeIsEnum(td) || sipTypeIsScopedEnum(td));

    if (sipTypeIsScopedEnum(td))
    {
        static PyObject *value = NULL;
        PyObject *val_obj;

        if (PyObject_IsInstance(obj, (PyObject *)sipTypeAsPyTypeObject(td)) <= 0)
        {
            enum_expected(obj, td);
            return -1;
        }

        if (sip_objectify("value", &value) < 0)
            return -1;

        if ((val_obj = PyObject_GetAttr(obj, value)) == NULL)
            return -1;

        /* This will never overflow. */
        val = sip_api_long_as_int(val_obj);

        Py_DECREF(val_obj);
    }
    else
    {
        if (PyObject_TypeCheck((PyObject *)Py_TYPE(obj), &sipEnumType_Type))
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
static PyObject *create_scoped_enum(sipExportedModuleDef *client,
        sipEnumTypeDef *etd, int enum_nr, PyObject *name)
{
    static PyObject *module_arg = NULL;
    static PyObject *qualname_arg = NULL;
    int i, nr_members;
    sipEnumMemberDef *enm;
    PyObject *members, *enum_obj, *args, *kw_args;

    /* Get the IntEnum type if we haven't done so already. */
    if (init_enum_module_types() < 0)
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

    if (sip_objectify("module", &module_arg) < 0)
        goto rel_kw_args;

    if (PyDict_SetItem(kw_args, module_arg, client->em_nameobj) < 0)
        goto rel_kw_args;

    /*
     * If the enum has a scope then the default __qualname__ will be incorrect.
     */
     if (etd->etd_scope >= 0)
     {
        int rc;
        PyObject *qualname;

        if (sip_objectify("qualname", &qualname_arg) < 0)
            goto rel_kw_args;

        if ((qualname = sip_get_qualname(client->em_types[etd->etd_scope], name)) == NULL)
            goto rel_kw_args;

        rc = PyDict_SetItem(kw_args, qualname_arg, qualname);

        Py_DECREF(qualname);

        if (rc < 0)
            goto rel_kw_args;
    }

    if ((enum_obj = PyObject_Call(int_enum_type, args, kw_args)) == NULL)
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
static PyObject *create_unscoped_enum(sipExportedModuleDef *client,
        sipEnumTypeDef *etd, PyObject *name)
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
    assert(currentEnum == NULL);
    currentEnum = &etd->etd_base;
    eto = (sipEnumTypeObject *)PyObject_Call((PyObject *)&sipEnumType_Type,
            args, NULL);
    currentEnum = NULL;

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
static int init_enum_module_types()
{
    PyObject *enum_module;

    /* Check if it has already been done. */
    if (enum_type != NULL)
        return 0;

    /* Get the enum types. */
    if ((enum_module = PyImport_ImportModule("enum")) == NULL)
        return -1;

    enum_type = PyObject_GetAttrString(enum_module, "Enum");
    int_enum_type = PyObject_GetAttrString(enum_module, "IntEnum");

    Py_DECREF(enum_module);

    if (enum_type == NULL || int_enum_type == NULL)
    {
        Py_XDECREF(enum_type);
        Py_XDECREF(int_enum_type);

        return -1;
    }

    return 0;
}


/*
 * The enum type alloc slot.
 */
static PyObject *sipEnumType_alloc(PyTypeObject *self, Py_ssize_t nitems)
{
    sipEnumTypeObject *py_type;
    sipPySlotDef *psd;

    if (currentEnum == NULL)
    {
        PyErr_SetString(PyExc_TypeError, "enums cannot be sub-classed");
        return NULL;
    }

    assert(sipTypeIsEnum(currentEnum));

    /* Call the standard super-metatype alloc. */
    if ((py_type = (sipEnumTypeObject *)PyType_Type.tp_alloc(self, nitems)) == NULL)
        return NULL;

    /*
     * Set the links between the Python type object and the generated type
     * structure.  Strictly speaking this doesn't need to be done here.
     */
    py_type->type = currentEnum;
    currentEnum->td_py_type = (PyTypeObject *)py_type;

    /*
     * Initialise any slots.  This must be done here, after the type is
     * allocated but before PyType_Ready() is called.
     */
    if ((psd = ((sipEnumTypeDef *)currentEnum)->etd_pyslots) != NULL)
        sip_add_type_slots(&py_type->super, psd);

    return (PyObject *)py_type;
}


/*
 * The enum type getattro slot.
 */
static PyObject *sipEnumType_getattro(PyObject *self, PyObject *name)
{
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
            return sip_api_convert_from_enum(emd->em_val, (sipTypeDef *)etd);

    PyErr_Format(PyExc_AttributeError,
            _SIP_MODULE_FQ_NAME ".enumtype object '%s' has no member '%s'",
            sipPyNameOfEnum(etd), name_str);

    return NULL;
}

#endif
