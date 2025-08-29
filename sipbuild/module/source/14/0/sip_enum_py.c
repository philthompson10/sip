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
#include "sip_wrapper_type.h"


#define IS_UNSIGNED_ENUM(etd)   ((etd)->etd_base_type == SIP_ENUM_UINT_ENUM || (etd)->etd_base_type == SIP_ENUM_INT_FLAG || (etd)->etd_base_type == SIP_ENUM_FLAG)


/* Forward references. */
#if 0
static PyObject *create_enum_object(sipWrappedModuleState *wms,
        const sipEnumTypeDef *etd, const sipIntInstanceDef **next_int_p,
        PyObject *name);
#endif
static PyTypeObject *get_enum_type(sipWrappedModuleState *wms,
        sipTypeID type_id);
static PyObject *missing(PyObject *cls, PyObject *value, int int_enum);
static PyObject *missing_enum(PyObject *cls, PyObject *value);
static PyObject *missing_int_enum(PyObject *cls, PyObject *value);


/*
 * Implement the creation of a Python object for a member of a named enum.
 */
PyObject *sip_enum_convert_from_enum(sipWrappedModuleState *wms, int member,
        sipTypeID type_id)
{
    assert(sipTypeIDIsEnumPy(type_id));

    const sipTypeDef *td = sip_get_type_def(wms, type_id, NULL);

    PyTypeObject *et = get_enum_type(wms, type_id);

    return PyObject_CallFunction((PyObject *)et,
            IS_UNSIGNED_ENUM((sipEnumTypeDef *)td) ? "(I)" : "(i)", member);
}


/*
 * Implement the conversion from a Python object implementing an enum to an
 * integer value.
 */
int sip_enum_convert_to_enum(sipWrappedModuleState *wms, PyObject *obj,
        sipTypeID type_id)
{
    assert(sipTypeIDIsEnumPy(type_id));

    const sipTypeDef *td = sip_get_type_def(wms, type_id, NULL);

    /* Make sure the enum object has been created. */
    PyTypeObject *py_type = get_enum_type(wms, type_id);

    /* Check the type of the Python object. */
    if (PyObject_IsInstance(obj, (PyObject *)py_type) <= 0)
    {
        PyErr_Format(PyExc_TypeError,
                "a member of enum '%s' is expected not '%s'", py_type->tp_name,
                Py_TYPE(obj)->tp_name);

        return -1;
    }

    /* Get the value from the object. */
    PyObject *value_s = PyUnicode_InternFromString("value");

    if (value_s == NULL)
        return -1;

    PyObject *val_obj = PyObject_GetAttr(obj, value_s);
    Py_DECREF(value_s);

    if (val_obj == NULL)
        return -1;

    /* Flags are implicitly unsigned. */
    int val;

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
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    return (PyObject_IsSubclass(obj, wms->sip_module_state->enum_flag_type) == 1);
}


/*
 * Convert a Python object implementing a constrained enum to an integer value.
 */
int sip_enum_convert_to_constrained_enum(sipWrappedModuleState *wms,
        PyObject *obj, sipTypeID type_id)
{
    /* There is no difference between constrained and unconstrained enums. */
    return sip_enum_convert_to_enum(wms, obj, type_id);
}


/*
 * Create an enum object and add it to a dictionary.  Return a new reference to
 * the enum object or NULL (and an exception set) if there was an error.
 */
#if 0
PyTypeObject *sip_enum_create_py_enum(sipWrappedModuleState *wms,
        const sipEnumTypeDef *etd, const sipIntInstanceDef **next_int_p,
        PyObject *dict)
{
    /* Create an object corresponding to the type name. */
    PyObject *name = PyUnicode_FromString(etd->etd_name);
    if (name == NULL)
        return NULL;

    /* Create the enum object. */
    PyObject *enum_obj = create_enum_object(wms, etd, next_int_p, name);
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
#endif


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
#if 0
static PyObject *create_enum_object(sipWrappedModuleState *wms,
        const sipEnumTypeDef *etd, const sipIntInstanceDef **next_int_p,
        PyObject *name)
{
    sipSipModuleState *sms = wms->sip_module_state;
    int i, rc;
    PyObject *members, *enum_factory, *enum_obj, *args, *kw_args;
    PyMethodDef *missing_md;
    const sipIntInstanceDef *next_int;

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

    PyObject *module_name = PyModule_GetNameObject(wms->wrapped_module);

    if (module_name == NULL)
    {
        Py_DECREF(module_s);
        goto rel_kw_args;
    }

    rc = PyDict_SetItem(kw_args, module_s, module_name);
    Py_DECREF(module_s);
    Py_DECREF(module_name);

    if (rc < 0)
        goto rel_kw_args;

    /*
     * If the enum has a scope then the default __qualname__ will be incorrect.
     */
     // TODO Review the need for this.
#if 0
     if (etd->etd_scope >= 0)
     {
        PyObject *qualname;

        if ((qualname = sip_get_qualname(wmd->types[etd->etd_scope], name)) == NULL)
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
#endif

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
#endif


/*
 * Get the Python object for an enum type.
 */
static PyTypeObject *get_enum_type(sipWrappedModuleState *wms,
        sipTypeID type_id)
{
    PyTypeObject *py_type = sip_get_py_type(wms, type_id);

#if 0
    /* Make sure the enum object has been created. */
    if (py_type == NULL)
    {
        PyTypeObject *scope_py_type = sip_get_py_type(wms,
                sip_type_scope(wms, type_id));
        const sipTypeDef *scope_td = ((sipWrapperType *)scope_py_type)->wt_td;

        if (sip_container_add_lazy_attrs(wms, scope_py_type, scope_td) < 0)
            return NULL;

        py_type = sip_get_py_type(wms, type_id);

        assert(py_type != NULL);
    }
#endif

    return py_type;
}


/*
 * The bulk of the implementation of _missing_ that handles missing members.
 */
static PyObject *missing(PyObject *cls, PyObject *value, int int_enum)
{
    sipSipModuleState *sms = sip_get_sip_module_state_from_sip_type(
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
