/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file implements the enum support using Python enums.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip.h"

#if defined(SIP_CONFIGURATION_PyEnums)

#include <assert.h>

#include "sip_enum.h"

#include "sip_core.h"
#include "sip_int_convertors.h"
#include "sip_module.h"


#define IS_UNSIGNED_ENUM(etd)   ((etd)->etd_base_type == SIP_ENUM_UINT_ENUM || (etd)->etd_base_type == SIP_ENUM_INT_FLAG || (etd)->etd_base_type == SIP_ENUM_FLAG)


/* Forward references. */
static PyObject *create_enum_object(sipSipModuleState *sms,
        sipWrappedModuleDef *client, sipEnumTypeDef *etd,
        sipIntInstanceDef **next_int_p, PyObject *name);
static void enum_expected(PyObject *obj, const sipTypeDef *td);
static PyObject *get_enum_type(sipSipModuleState *sms, const sipTypeDef *td);
static PyObject *missing(PyObject *cls, PyObject *value, int int_enum);
static PyObject *missing_enum(PyObject *cls, PyObject *value);
static PyObject *missing_int_enum(PyObject *cls, PyObject *value);


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
PyObject *sip_enum_convert_from_enum(sipSipModuleState *sms, int member,
        const sipTypeDef *td)
{
    PyObject *et;

    assert(sipTypeIsEnum(td));

    et = get_enum_type(sms, td);

    return PyObject_CallFunction(et,
            IS_UNSIGNED_ENUM((sipEnumTypeDef *)td) ? "(I)" : "(i)", member);
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
 * Implement the conversion from a Python object implementing an enum to an
 * integer value.
 */
int sip_enum_convert_to_enum(sipSipModuleState *sms, PyObject *obj,
        const sipTypeDef *td)
{
    PyObject *val_obj, *type_obj;
    int val;

    assert(sipTypeIsEnum(td));

    /* Make sure the enum object has been created. */
    type_obj = get_enum_type(sms, td);

    /* Check the type of the Python object. */
    if (PyObject_IsInstance(obj, type_obj) <= 0)
    {
        enum_expected(obj, td);
        return -1;
    }

    /* Get the value from the object. */
    PyObject *value_s = PyUnicode_InternFromString("value");

    if (value_s == NULL)
        return -1;

    val_obj = PyObject_GetAttr(obj, value_s);
    Py_DECREF(value_s);

    if (val_obj == NULL)
        return -1;

    /* Flags are implicitly unsigned. */
    if (IS_UNSIGNED_ENUM((sipEnumTypeDef *)td))
        val = (int)sip_api_long_as_unsigned_int(val_obj);
    else
        val = sip_api_long_as_int(val_obj);

    Py_DECREF(val_obj);

    return val;
}


/*
 * Return a non-zero value if an object is a sub-class of enum.Flag.
 */
int sip_api_is_enum_flag(PyObject *wmod, PyObject *obj)
{
    sipSipModuleState *sms = sip_get_sip_module_state(wmod);

    return (PyObject_IsSubclass(obj, sms->enum_flag_type) == 1);
}


/*
 * Convert a Python object implementing a constrained enum to an integer value.
 */
int sip_enum_convert_to_constrained_enum(sipSipModuleState *sms, PyObject *obj,
        const sipTypeDef *td)
{
    /* There is no difference between constrained and unconstrained enums. */
    return sip_enum_convert_to_enum(sms, obj, td);
}


/*
 * Create an enum object and add it to a dictionary.  A negative value is
 * returned (and an exception set) if there was an error.
 */
int sip_enum_create_py_enum(sipSipModuleState *sms,
        sipWrappedModuleDef *client, sipEnumTypeDef *etd,
        sipIntInstanceDef **next_int_p, PyObject *dict)
{
    int rc;
    PyObject *name, *enum_obj;

    /* Create an object corresponding to the type name. */
    if ((name = PyUnicode_FromString(sipPyNameOfEnum(etd))) == NULL)
        return -1;

    /* Create the enum object. */
    if ((enum_obj = create_enum_object(sms, client, etd, next_int_p, name)) == NULL)
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
 * Return the generated type structure for a Python enum object that wraps a
 * C/C++ enum or NULL (and no exception set) if the object is something else.
 */
const sipTypeDef *sip_enum_get_generated_type(sipSipModuleState *sms,
        PyTypeObject *py_type)
{
    if (sip_enum_is_enum(sms, (PyObject *)py_type))
    {
        PyObject *dunder_sip = PyUnicode_InternFromString("__sip__");

        if (dunder_sip == NULL)
            return NULL;

        PyObject *etd_cap = PyObject_GetAttr((PyObject *)py_type, dunder_sip);
        Py_DECREF(dunder_sip);

        if (etd_cap != NULL)
        {
            sipTypeDef *td = (sipTypeDef *)PyCapsule_GetPointer(etd_cap, NULL);

            Py_DECREF(etd_cap);

            return td;
        }

        PyErr_Clear();
    }

    return NULL;
}


/*
 * Initialise the enum support.  A negative value is returned (and an exception
 * set) if there was an error.
 */
int sip_enum_init(PyObject *module, sipSipModuleState *sms)
{
    /* Get the containers of the types */
    PyObject *enum_module = PyImport_ImportModule("enum");

    if (enum_module == NULL)
        return -1;

    PyObject *builtins = PyEval_GetBuiltins();

    /* Get the builtin types. */
    sms->builtin_int_type = PyDict_GetItemString(builtins, "int");
    sms->builtin_object_type = PyDict_GetItemString(builtins, "object");

    if (sms->builtin_int_type == NULL || sms->builtin_object_type == NULL)
    {
        Py_XDECREF(sms->builtin_int_type);
        Py_XDECREF(sms->builtin_object_type);

        Py_DECREF(enum_module);

        return -1;
    }

    /* Get the enum types. */
    sms->enum_enum_type = PyObject_GetAttrString(enum_module, "Enum");
    sms->enum_int_enum_type = PyObject_GetAttrString(enum_module, "IntEnum");
    sms->enum_flag_type = PyObject_GetAttrString(enum_module, "Flag");
    sms->enum_int_flag_type = PyObject_GetAttrString(enum_module, "IntFlag");

    Py_DECREF(enum_module);

    if (sms->enum_enum_type == NULL || sms->enum_int_enum_type == NULL || sms->enum_flag_type == NULL || sms->enum_int_flag_type == NULL)
    {
        Py_XDECREF(sms->enum_enum_type);
        Py_XDECREF(sms->enum_int_enum_type);
        Py_XDECREF(sms->enum_flag_type);
        Py_XDECREF(sms->enum_int_flag_type);

        Py_DECREF(sms->builtin_int_type);
        Py_DECREF(sms->builtin_object_type);

        return -1;
    }

    return 0;
}


/*
 * Return a non-zero value if an object is a sub-class of enum.Enum.
 */
int sip_enum_is_enum(sipSipModuleState *sms, PyObject *obj)
{
    return (PyObject_IsSubclass(obj, sms->enum_enum_type) == 1);
}


/*
 * Create an enum object.
 */
static PyObject *create_enum_object(sipSipModuleState *sms,
        sipWrappedModuleDef *client, sipEnumTypeDef *etd,
        sipIntInstanceDef **next_int_p, PyObject *name)
{
    int i, rc;
    PyObject *members, *enum_factory, *enum_obj, *args, *kw_args, *etd_cap;
    PyMethodDef *missing_md;
    sipIntInstanceDef *next_int;

    /* Create a dict of the members. */
    if ((members = PyDict_New()) == NULL)
        goto ret_err;

    next_int = *next_int_p;
    assert(next_int != NULL);

    for (i = 0; i < etd->etd_nr_members; ++i)
    {
        PyObject *value_obj;

        assert(next_int->ii_name != NULL);

        /* Flags are implicitly unsigned. */
        if (IS_UNSIGNED_ENUM(etd))
            value_obj = PyLong_FromUnsignedLong((unsigned)next_int->ii_val);
        else
            value_obj = PyLong_FromLong(next_int->ii_val);

        if (sip_dict_set_and_discard(members, next_int->ii_name, value_obj) < 0)
            goto rel_members;

        ++next_int;
    }

    *next_int_p = next_int;

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
     if (etd->etd_scope >= 0)
     {
        PyObject *qualname;

        if ((qualname = sip_get_qualname(client->em_types[etd->etd_scope], name)) == NULL)
            goto rel_kw_args;

        PyObject *qualname_s = PyUnicode_InternFromString("qualname");

        if (qualname_s == NULL)
        {
            Py_DECREF(qualname);
            goto rel_kw_args;
        }

        rc = PyDict_SetItem(kw_args, qualname_s, qualname);
        Py_DECREF(qualname_s);
        Py_DECREF(qualname);

        if (rc < 0)
            goto rel_kw_args;
    }

    missing_md = NULL;

    if (etd->etd_base_type == SIP_ENUM_INT_FLAG)
    {
        enum_factory = sms->enum_int_flag_type;
    }
    else if (etd->etd_base_type == SIP_ENUM_FLAG)
    {
        enum_factory = sms->enum_flag_type;
    }
    else if (etd->etd_base_type == SIP_ENUM_INT_ENUM || etd->etd_base_type == SIP_ENUM_UINT_ENUM)
    {
        static PyMethodDef missing_int_enum_md = {
            "_missing_", missing_int_enum, METH_O|METH_CLASS, NULL
        };

        enum_factory = sms->enum_int_enum_type;
        missing_md = &missing_int_enum_md;
    }
    else
    {
        static PyMethodDef missing_enum_md = {
            "_missing_", missing_enum, METH_O|METH_CLASS, NULL
        };

        enum_factory = sms->enum_enum_type;
        missing_md = &missing_enum_md;
    }

    if ((enum_obj = PyObject_Call(enum_factory, args, kw_args)) == NULL)
        goto rel_kw_args;

    Py_DECREF(kw_args);
    Py_DECREF(args);
    Py_DECREF(members);

    etd->etd_base.td_py_type = (PyTypeObject *)enum_obj;

    /* Inject _missing_. */
    if (missing_md != NULL)
    {
        PyObject *missing_cfunc;

        if ((missing_cfunc = PyCFunction_New(missing_md, enum_obj)) == NULL)
        {
            Py_DECREF(enum_obj);
            return NULL;
        }

        PyObject *sunder_missing = PyUnicode_InternFromString("_missing_");

        if (sunder_missing == NULL)
        {
            Py_DECREF(missing_cfunc);
            Py_DECREF(enum_obj);
            return NULL;
        }

        rc = PyObject_SetAttr(enum_obj, sunder_missing, missing_cfunc);
        Py_DECREF(sunder_missing);
        Py_DECREF(missing_cfunc);

        if (rc < 0)
        {
            Py_DECREF(enum_obj);
            return NULL;
        }
    }

    /* Wrap the generated type definition in a capsule. */
    if ((etd_cap = PyCapsule_New(etd, NULL, NULL)) == NULL)
    {
        Py_DECREF(enum_obj);
        return NULL;
    }

    PyObject *dunder_sip = PyUnicode_InternFromString("__sip__");

    if (dunder_sip == NULL)
    {
        Py_DECREF(etd_cap);
        Py_DECREF(enum_obj);
        return NULL;
    }

    rc = PyObject_SetAttr(enum_obj, dunder_sip, etd_cap);
    Py_DECREF(dunder_sip);
    Py_DECREF(etd_cap);

    if (rc < 0)
    {
        Py_DECREF(enum_obj);
        return NULL;
    }

    if (etd->etd_pyslots != NULL)
        sip_add_type_slots((PyHeapTypeObject *)enum_obj, etd->etd_pyslots);

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
 * Raise an exception when failing to convert an enum because of its type.
 */
static void enum_expected(PyObject *obj, const sipTypeDef *td)
{
    PyErr_Format(PyExc_TypeError, "a member of enum '%s' is expected not '%s'",
            sipPyNameOfEnum((sipEnumTypeDef *)td), Py_TYPE(obj)->tp_name);
}


/*
 * Get the Python object for an enum type.
 */
static PyObject *get_enum_type(sipSipModuleState *sms, const sipTypeDef *td)
{
    PyObject *type_obj;

    /* Make sure the enum object has been created. */
    type_obj = (PyObject *)sipTypeAsPyTypeObject(td);

    if (type_obj == NULL)
    {
        if (sip_add_all_lazy_attrs(sms, sip_api_type_scope(td)) < 0)
            return NULL;

        type_obj = (PyObject *)sipTypeAsPyTypeObject(td);
    }

    return type_obj;
}


/*
 * The bulk of the implementation of _missing_ that handles missing members.
 */
static PyObject *missing(PyObject *cls, PyObject *value, int int_enum)
{
    sipSipModuleState *sms = sip_get_sip_module_state_from_wrapper_type(
            (PyTypeObject *)cls);
    PyObject *sip_missing, *member, *value_str;
    int rc;

    PyObject *sunder_sip_missing = PyUnicode_InternFromString("_sip_missing_");

    if (sunder_sip_missing == NULL)
        return NULL;

    /* Get the dict of previously missing members. */
    if ((sip_missing = PyObject_GetAttr(cls, sunder_sip_missing)) != NULL)
    {
        member = PyDict_GetItemWithError(sip_missing, value);
        Py_DECREF(sunder_sip_missing);

        if (member != NULL)
        {
            /* Return the already missing member. */
            Py_INCREF(member);
            return member;
        }

        /* A missing key will not raise an exception. */
        if (PyErr_Occurred())
        {
            Py_DECREF(sip_missing);
            return NULL;
        }
    }
    else if (PyErr_ExceptionMatches(PyExc_AttributeError))
    {
        PyErr_Clear();

        /* Create the dict and save it in the class. */
        if ((sip_missing = PyDict_New()) == NULL)
        {
            Py_DECREF(sunder_sip_missing);
            return NULL;
        }

        rc = PyObject_SetAttr(cls, sunder_sip_missing, sip_missing);
        Py_DECREF(sunder_sip_missing);

        if (rc < 0)
        {
            Py_DECREF(sip_missing);
            return NULL;
        }
    }
    else
    {
        Py_DECREF(sunder_sip_missing);

        /* The exception is unexpected. */
        return NULL;
    }

    /* Create a member for the missing value. */
    PyObject *dunder_new = PyUnicode_InternFromString("__new__");

    if (dunder_new == NULL)
    {
        Py_DECREF(sip_missing);
        return NULL;
    }

    if (int_enum)
        member = PyObject_CallMethodObjArgs(sms->builtin_int_type, dunder_new, cls, value,
                NULL);
    else
        member = PyObject_CallMethodObjArgs(sms->builtin_object_type, dunder_new, cls,
                NULL);

    Py_DECREF(dunder_new);

    if (member == NULL)
    {
        Py_DECREF(sip_missing);
        return NULL;
    }

    /* Set the member's attributes. */
    if ((value_str = PyObject_Str(value)) == NULL)
    {
        Py_DECREF(member);
        Py_DECREF(sip_missing);
        return NULL;
    }

    PyObject *sunder_name = PyUnicode_InternFromString("_name_");

    if (sunder_name == NULL)
    {
        Py_DECREF(value_str);
        Py_DECREF(member);
        Py_DECREF(sip_missing);
        return NULL;
    }

    rc = PyObject_SetAttr(member, sunder_name, value_str);
    Py_DECREF(sunder_name);
    Py_DECREF(value_str);

    if (rc < 0)
    {
        Py_DECREF(member);
        Py_DECREF(sip_missing);
        return NULL;
    }

    PyObject *sunder_value = PyUnicode_InternFromString("_value_");

    if (sunder_value == NULL)
    {
        Py_DECREF(member);
        Py_DECREF(sip_missing);
    }

    rc = PyObject_SetAttr(member, sunder_value, value);
    Py_DECREF(sunder_value);

    if (rc < 0)
    {
        Py_DECREF(member);
        Py_DECREF(sip_missing);
        return NULL;
    }

    /* Save the member so that it is a singleton. */
    rc = PyDict_SetItem(sip_missing, value, member);
    Py_DECREF(sip_missing);

    if (rc < 0)
    {
        Py_DECREF(member);
        return NULL;
    }

    return member;
}


/*
 * The replacement implementation of _missing_ that handles missing members in
 * Enums.
 */
static PyObject *missing_enum(PyObject *cls, PyObject *value)
{
    return missing(cls, value, FALSE);
}


/*
 * The replacment implementation of _missing_ that handles missing members in
 * IntEnums.
 */
static PyObject *missing_int_enum(PyObject *cls, PyObject *value)
{
    return missing(cls, value, TRUE);
}

#endif
