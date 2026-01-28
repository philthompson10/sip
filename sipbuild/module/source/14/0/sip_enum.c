/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file implements the enum support for all styles of enum.
 *
 * Copyright (c) 2026 Phil Thompson <phil@riverbankcomputing.com>
 */


#include <Python.h>

#include "sip_enum.h"

#include "sip.h"
#include "sip_core.h"
#include "sip_int_convertors.h"
#include "sip_module.h"
#include "sip_wrapped_module.h"


#if defined(SIP_CONFIGURATION_CustomEnums)

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
             Py_TPFLAGS_DISALLOW_INSTANTIATION |
             Py_TPFLAGS_IMMUTABLETYPE |
             Py_TPFLAGS_HAVE_GC,
    .slots = EnumType_slots,
};


/*
 * The custom enum type's alloc slot.
 */
static PyObject *EnumType_alloc(PyTypeObject *self, Py_ssize_t nitems)
{
    sipSipModuleState *sms = sip_get_sip_module_state_from_type(self);
    sipEnumTypeObject *py_type;

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

    return (PyObject *)py_type;
}


/*
 * The custom enum type's dealloc slot.
 */
static void EnumType_dealloc(PyObject *self)
{
    PyObject_GC_UnTrack(self);
    PyTypeObject *type = Py_TYPE(self);
    type->tp_free(self);
    Py_DECREF(type);
}


/*
 * The custom enum type's getattro slot.
 */
static PyObject *EnumType_getattro(PyObject *self, PyObject *name)
{
    // TODO
#if 0
    sipSipModuleState *sms = sip_get_sip_module_state_from_type(Py_TYPE(self));
    PyObject *res;
    sipEnumTypeSpec *etd;
    const sipModuleSpec *wmd;
    const sipEnumMemberSpec *enm, *emd;
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

    etd = (sipEnumTypeSpec *)((sipEnumTypeObject *)self)->type;
    wmd = ((sipTypeSpec *)etd)->td_module;

    /* Find the number of this enum. */
    for (enum_nr = 0; enum_nr < wmd->nr_types; ++enum_nr)
        if (wmd->types[enum_nr] == (sipTypeSpec *)etd)
            break;

    /* Get the enum members in the same scope. */
    if (etd->scope_nr < 0)
    {
        nr_members = wmd->nr_enum_members;
        enm = wmd->enum_members;
    }
    else
    {
        const sipContainerSpec *cod = sip_get_container(
                wmd->types[etd->scope_nr]);

        nr_members = cod->nr_enum_members;
        enm = cod->enum_members;
    }

    /* Find the enum member. */
    for (emd = enm, m = 0; m < nr_members; ++m, ++emd)
        if (emd->enum_nr == enum_nr && strcmp(emd->name, name_str) == 0)
            return sip_enum_convert_from_enum(sms, emd->value,
                    (sipTypeSpec *)etd);

    PyErr_Format(PyExc_AttributeError,
            _SIP_MODULE_FQ_NAME ".enumtype object '%s' has no member '%s'",
            etd->py_name, name_str);
#endif

    return NULL;
}


/*
 * The custom enum type's traverse slot.
 */
static int EnumType_traverse(PyObject *self, visitproc visit, void *arg)
{
    Py_VISIT(Py_TYPE(self));

    return 0;
}

#endif


/* Forward declarations. */
#if defined(SIP_CONFIGURATION_CustomEnums)
static PyTypeObject *create_custom_enum_type(const sipEnumTypeSpec *ets,
        PyObject *name);
#endif
static PyTypeObject *create_py_enum_type(sipModuleState *ms,
        const sipEnumTypeSpec *ets, PyObject *name);
static int init_enum_module_types(sipSipModuleState *sms);
#if defined(SIP_CONFIGURATION_PyEnums)
static void add_operator_slots(PyObject *enum_obj, const PyType_Slot *pts);
static PyObject *missing(PyObject *cls, PyObject *value, int int_enum);
static PyObject *missing_enum(PyObject *cls, PyObject *value);
static PyObject *missing_int_enum(PyObject *cls, PyObject *value);
#endif
static void raise_internal_error(sipTypeID type_id);


#if defined(SIP_CONFIGURATION_PyEnums)
/*
 * Return a non-zero value if an object is a sub-class of enum.Flag.
 */
int sip_api_is_enum_flag(PyObject *mod, PyObject *obj)
{
    sipSipModuleState *sms = sip_get_module_state(mod)->sip_module_state;

    return (PyObject_IsSubclass(obj, sms->enum_flag_type) == 1);
}
#endif


/*
 * Create the Python type object for a wrapped enum.
 */
PyTypeObject *sip_create_enum_type(sipModuleState *ms, sipTypeNr type_nr,
        const sipEnumTypeSpec *ets)
{
    /* Get the enum types if we haven't done so already. */
    if (init_enum_module_types(ms->sip_module_state) < 0)
        return NULL;

    PyObject *name = PyUnicode_FromString(strrchr(ets->fq_py_name, '.') + 1);
    if (name == NULL)
        return NULL;

    PyTypeObject *enum_type;

#if defined(SIP_CONFIGURATION_PyEnums)
    enum_type = create_py_enum_type(ms, ets, name);
#endif

#if defined(SIP_CONFIGURATION_CustomEnums)
    if (sipTypeIsScopedEnum((const sipTypeSpec *)ets))
        enum_type = create_py_enum_type(ms, ets, name);
    else
        enum_type = create_custom_enum_type(ets, name);
#endif

    Py_DECREF(name);

    return enum_type;
}


/*
 * Implement the creation of a Python object for a member of a named enum.
 */
PyObject *sip_enum_convert_from_enum(sipModuleState *ms, void *addr,
        sipTypeID type_id)
{
    // TODO Custom support.
    assert(sipTypeIDIsEnumPy(type_id));

    PyObject *py_type;
    const sipEnumTypeSpec *ets = (const sipEnumTypeSpec *)sip_get_type_detail(
            ms, type_id, (PyTypeObject **)&py_type, NULL);

    switch (ets->cpp_base_type)
    {
        case sipTypeID_byte:
            return PyObject_CallFunction(py_type, "(b)", *(char *)addr);

        case sipTypeID_sbyte:
            return PyObject_CallFunction(py_type, "(b)", *(signed char *)addr);

        case sipTypeID_ubyte:
            return PyObject_CallFunction(py_type, "(B)",
                    *(unsigned char *)addr);

        case sipTypeID_short:
            return PyObject_CallFunction(py_type, "(h)", *(short *)addr);

        case sipTypeID_ushort:
            return PyObject_CallFunction(py_type, "(H)",
                    *(unsigned short *)addr);

        case sipTypeID_int:
            return PyObject_CallFunction(py_type, "(i)", *(int *)addr);

        case sipTypeID_uint:
            return PyObject_CallFunction(py_type, "(I)", *(unsigned *)addr);

        case sipTypeID_long:
            return PyObject_CallFunction(py_type, "(l)", *(long *)addr);

        case sipTypeID_ulong:
            return PyObject_CallFunction(py_type, "(k)",
                    *(unsigned long *)addr);

        case sipTypeID_longlong:
            return PyObject_CallFunction(py_type, "(L)", *(long long *)addr);

        case sipTypeID_ulonglong:
            return PyObject_CallFunction(py_type, "(K)",
                    *(unsigned long long *)addr);

        default:
            break;
    }

    raise_internal_error(ets->cpp_base_type);

    return NULL;
}


/*
 * Convert a Python object implementing a constrained enum to an integer value.
 */
int sip_enum_convert_to_constrained_enum(sipModuleState *ms, PyObject *obj,
        void *addr, sipTypeID type_id)
{
    // TODO Custom support.
    /* There is no difference between constrained and unconstrained enums. */
    return sip_enum_convert_to_enum(ms, obj, addr, type_id);
}


/*
 * Implement the conversion from a Python object implementing an enum to a
 * member value.
 */
int sip_enum_convert_to_enum(sipModuleState *ms, PyObject *obj, void *addr,
        sipTypeID type_id)
{
    // TODO Custom support.
    assert(sipTypeIDIsEnumPy(type_id));

    PyTypeObject *py_type;
    const sipEnumTypeSpec *ets = (const sipEnumTypeSpec *)sip_get_type_detail(
            ms, type_id, &py_type, NULL);

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

    /* Convert the value. */
    PyErr_Clear();

    switch (ets->cpp_base_type)
    {
        case sipTypeID_byte:
            *(char *)addr = sip_api_long_as_char(val_obj);
            break;

        case sipTypeID_sbyte:
            *(signed char *)addr = sip_api_long_as_signed_char(val_obj);
            break;

        case sipTypeID_ubyte:
            *(unsigned char *)addr = sip_api_long_as_unsigned_char(val_obj);
            break;

        case sipTypeID_short:
            *(short *)addr = sip_api_long_as_short(val_obj);
            break;

        case sipTypeID_ushort:
            *(unsigned short *)addr = sip_api_long_as_unsigned_short(val_obj);
            break;

        case sipTypeID_int:
            *(int *)addr = sip_api_long_as_int(val_obj);
            break;

        case sipTypeID_uint:
            *(unsigned *)addr = sip_api_long_as_unsigned_int(val_obj);
            break;

        case sipTypeID_long:
            *(long *)addr = sip_api_long_as_long(val_obj);
            break;

        case sipTypeID_ulong:
            *(unsigned long *)addr = sip_api_long_as_unsigned_long(val_obj);
            break;

        case sipTypeID_longlong:
            *(long long *)addr = sip_api_long_as_long_long(val_obj);
            break;

        case sipTypeID_ulonglong:
            *(unsigned long long *)addr = sip_api_long_as_unsigned_long_long(
                    val_obj);
            break;

        default:
            raise_internal_error(ets->cpp_base_type);
    }

    Py_DECREF(val_obj);

    return PyErr_Occurred() ? -1 : 0;
}


/*
 * Initialise the enum support.  A negative value is returned (and an exception
 * set) if there was an error.
 */
int sip_enum_init(PyObject *mod, sipSipModuleState *sms)
{
#if defined(SIP_CONFIGURATION_PyEnums)
    sms->builtin_int_type = NULL;
    sms->builtin_object_type = NULL;
#endif

    sms->enum_enum_type = NULL;
    sms->enum_int_enum_type = NULL;
#if defined(SIP_CONFIGURATION_PyEnums)
    sms->enum_flag_type = NULL;
    sms->enum_int_flag_type = NULL;
#endif

#if defined(SIP_CONFIGURATION_CustomEnums)
    sms->custom_enum_type = (PyTypeObject *)PyType_FromModuleAndSpec(mod,
            &EnumType_TypeSpec, NULL);

    if (sms->custom_enum_type == NULL)
        return -1;

    sms->current_enum_backdoor = NULL;
#endif

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


#if defined(SIP_CONFIGURATION_CustomEnums)
/*
 * Create a custom enum type.
 */
static PyTypeObject *create_custom_enum_type(const sipEnumTypeSpec *ets,
        PyObject *name)
{
    // TODO
    return NULL;
}
#endif


/*
 * Create a Python enum type.
 */
static PyTypeObject *create_py_enum_type(sipModuleState *ms,
        const sipEnumTypeSpec *ets, PyObject *name)
{
    sipSipModuleState *sms = ms->sip_module_state;

    /* Create a dict of the members. */
    PyObject *members = PyDict_New();
    if (members == NULL)
        goto ret_err;

    const sipEnumMemberSpec *member = ets->members;

    while (member->name != NULL)
    {
        PyObject *value_obj;

#if defined(SIP_CONFIGURATION_PyEnums)
        switch (ets->cpp_base_type)
        {
            case sipTypeID_bool:
                value_obj = PyBool_FromLong(member->value.bool_t);
                break;

            case sipTypeID_byte:
                value_obj = PyLong_FromLong(member->value.byte_t);
                break;

            case sipTypeID_sbyte:
                value_obj = PyLong_FromLong(member->value.sbyte_t);
                break;

            case sipTypeID_ubyte:
                value_obj = PyLong_FromUnsignedLong(member->value.ubyte_t);
                break;

            case sipTypeID_short:
                value_obj = PyLong_FromLong(member->value.short_t);
                break;

            case sipTypeID_ushort:
                value_obj = PyLong_FromUnsignedLong(member->value.ushort_t);
                break;

            case sipTypeID_int:
                value_obj = PyLong_FromLong(member->value.int_t);
                break;

            case sipTypeID_uint:
                value_obj = PyLong_FromUnsignedLong(member->value.uint_t);
                break;

            case sipTypeID_long:
                value_obj = PyLong_FromLong(member->value.long_t);
                break;

            case sipTypeID_ulong:
                value_obj = PyLong_FromUnsignedLong(member->value.ulong_t);
                break;

            case sipTypeID_longlong:
                value_obj = PyLong_FromLongLong(member->value.longlong_t);
                break;

            case sipTypeID_ulonglong:
                value_obj = PyLong_FromUnsignedLongLong(
                        member->value.ulonglong_t);
                break;

            default:
                raise_internal_error(ets->cpp_base_type);
                value_obj = NULL;
        }
#endif

#if defined(SIP_CONFIGURATION_CustomEnums)
        value_obj = PyLong_FromLong(member->value);
#endif

        if (sip_dict_set_and_discard(members, member->name, value_obj) < 0)
            goto rel_members;

        member++;
    }

    PyObject *args = PyTuple_Pack(2, name, members);
    if (args == NULL)
        goto rel_members;

    PyObject *kw_args = PyDict_New();
    if (kw_args == NULL)
        goto rel_args;

    PyObject *module_s = PyUnicode_InternFromString("module");
    if (module_s == NULL)
        goto rel_kw_args;

    PyObject *module_name = PyModule_GetNameObject(ms->wrapped_module);
    if (module_name == NULL)
    {
        Py_DECREF(module_s);
        goto rel_kw_args;
    }

    int rc = PyDict_SetItem(kw_args, module_s, module_name);
    Py_DECREF(module_s);
    Py_DECREF(module_name);

    if (rc < 0)
        goto rel_kw_args;

    PyObject *enum_factory;

#if defined(SIP_CONFIGURATION_PyEnums)
    PyMethodDef *missing_md = NULL;

    if (ets->py_base_type == SIP_ENUM_INT_FLAG)
    {
        enum_factory = sms->enum_int_flag_type;
    }
    else if (ets->py_base_type == SIP_ENUM_FLAG)
    {
        enum_factory = sms->enum_flag_type;
    }
    else if (ets->py_base_type == SIP_ENUM_INT_ENUM || ets->py_base_type == SIP_ENUM_UINT_ENUM)
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
#endif

#if defined(SIP_CONFIGURATION_CustomEnums)
    enum_factory = sms->enum_int_enum_type;
#endif

    PyObject *enum_obj = PyObject_Call(enum_factory, args, kw_args);
    if (enum_obj == NULL)
        goto rel_kw_args;

    Py_DECREF(kw_args);
    Py_DECREF(args);
    Py_DECREF(members);

    if (ets->scope_nr >= 0)
        if (sip_fix_type_attrs(ms, ets->fq_py_name, enum_obj) < 0)
        {
            Py_DECREF(enum_obj);
            return NULL;
        }

#if defined(SIP_CONFIGURATION_PyEnums)
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

    if (ets->py_slots != NULL)
        add_operator_slots(enum_obj, ets->py_slots);
#endif

    return (PyTypeObject *)enum_obj;

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


#if defined(SIP_CONFIGURATION_PyEnums)
/*
 * Add any Python slots that implement C++ operators to an enum type.
 */
static void add_operator_slots(PyObject *enum_obj, const PyType_Slot *pts)
{
    PyAsyncMethods *am = &((PyHeapTypeObject *)enum_obj)->as_async;
    PyMappingMethods *mp = &((PyHeapTypeObject *)enum_obj)->as_mapping;
    PyNumberMethods *nb = &((PyHeapTypeObject *)enum_obj)->as_number;
    PySequenceMethods *sq = &((PyHeapTypeObject *)enum_obj)->as_sequence;
    PyTypeObject *tp = &((PyHeapTypeObject *)enum_obj)->ht_type;

    while (pts->slot != 0)
    {
        void *f = pts->pfunc;

        switch (pts->slot)
        {
        case Py_am_aiter:
            am->am_aiter = (unaryfunc)f;
            break;

        case Py_am_anext:
            am->am_anext = (unaryfunc)f;
            break;

        case Py_am_await:
            am->am_await = (unaryfunc)f;
            break;

        case Py_mp_ass_subscript:
            mp->mp_ass_subscript = (objobjargproc)f;
            break;

        case Py_mp_length:
            mp->mp_length = (lenfunc)f;
            break;

        case Py_mp_subscript:
            mp->mp_subscript = (binaryfunc)f;
            break;

        case Py_nb_absolute:
            nb->nb_absolute = (unaryfunc)f;
            break;

        case Py_nb_add:
            nb->nb_add = (binaryfunc)f;
            break;

        case Py_nb_and:
            nb->nb_and = (binaryfunc)f;
            break;

        case Py_nb_bool:
            nb->nb_bool = (inquiry)f;
            break;

        case Py_nb_float:
            nb->nb_float = (unaryfunc)f;
            break;

        case Py_nb_floor_divide:
            nb->nb_floor_divide = (binaryfunc)f;
            break;

        case Py_nb_index:
            nb->nb_index = (unaryfunc)f;
            break;

        case Py_nb_inplace_add:
            nb->nb_inplace_add = (binaryfunc)f;
            break;

        case Py_nb_inplace_and:
            nb->nb_inplace_and = (binaryfunc)f;
            break;

        case Py_nb_inplace_floor_divide:
            nb->nb_inplace_floor_divide = (binaryfunc)f;
            break;

        case Py_nb_inplace_lshift:
            nb->nb_inplace_lshift = (binaryfunc)f;
            break;

        case Py_nb_inplace_matrix_multiply:
            nb->nb_inplace_matrix_multiply = (binaryfunc)f;
            break;

        case Py_nb_inplace_multiply:
            nb->nb_inplace_multiply = (binaryfunc)f;
            break;

        case Py_nb_inplace_or:
            nb->nb_inplace_or = (binaryfunc)f;
            break;

        case Py_nb_inplace_remainder:
            nb->nb_inplace_remainder = (binaryfunc)f;
            break;

        case Py_nb_inplace_rshift:
            nb->nb_inplace_rshift = (binaryfunc)f;
            break;

        case Py_nb_inplace_subtract:
            nb->nb_inplace_subtract = (binaryfunc)f;
            break;

        case Py_nb_inplace_true_divide:
            nb->nb_inplace_true_divide = (binaryfunc)f;
            break;

        case Py_nb_inplace_xor:
            nb->nb_inplace_xor = (binaryfunc)f;
            break;

        case Py_nb_int:
            nb->nb_int = (unaryfunc)f;
            break;

        case Py_nb_invert:
            nb->nb_invert = (unaryfunc)f;
            break;

        case Py_nb_lshift:
            nb->nb_lshift = (binaryfunc)f;
            break;

        case Py_nb_matrix_multiply:
            nb->nb_matrix_multiply = (binaryfunc)f;
            break;

        case Py_nb_multiply:
            nb->nb_multiply = (binaryfunc)f;
            break;

        case Py_nb_negative:
            nb->nb_negative = (unaryfunc)f;
            break;

        case Py_nb_or:
            nb->nb_or = (binaryfunc)f;
            break;

        case Py_nb_positive:
            nb->nb_positive = (unaryfunc)f;
            break;

        case Py_nb_remainder:
            nb->nb_remainder = (binaryfunc)f;
            break;

        case Py_nb_rshift:
            nb->nb_rshift = (binaryfunc)f;
            break;

        case Py_nb_subtract:
            nb->nb_subtract = (binaryfunc)f;
            break;

        case Py_nb_true_divide:
            nb->nb_true_divide = (binaryfunc)f;
            break;

        case Py_nb_xor:
            nb->nb_xor = (binaryfunc)f;
            break;

        case Py_sq_ass_item:
            sq->sq_ass_item = (ssizeobjargproc)f;
            break;

        case Py_sq_concat:
            sq->sq_concat = (binaryfunc)f;
            break;

        case Py_sq_contains:
            sq->sq_contains = (objobjproc)f;
            break;

        case Py_sq_inplace_concat:
            sq->sq_inplace_concat = (binaryfunc)f;
            break;

        case Py_sq_inplace_repeat:
            sq->sq_inplace_repeat = (ssizeargfunc)f;
            break;

        case Py_sq_item:
            sq->sq_item = (ssizeargfunc)f;
            break;

        case Py_sq_length:
            sq->sq_length = (lenfunc)f;
            break;

        case Py_sq_repeat:
            sq->sq_repeat = (ssizeargfunc)f;
            break;

        case Py_tp_call:
            tp->tp_call = (ternaryfunc)f;
            break;

        case Py_tp_hash:
            tp->tp_hash = (hashfunc)f;
            break;

        case Py_tp_iter:
            tp->tp_iter = (getiterfunc)f;
            break;

        case Py_tp_iternext:
            tp->tp_iternext = (iternextfunc)f;
            break;

        case Py_tp_repr:
            tp->tp_repr = (reprfunc)f;
            break;

        case Py_tp_richcompare:
            tp->tp_richcompare = (richcmpfunc)f;
            break;

        case Py_tp_setattro:
            tp->tp_setattro = (setattrofunc)f;
            break;

        case Py_tp_str:
            tp->tp_str = (reprfunc)f;
            break;

        default:
            break;
        }

        pts++;
    }
}
#endif


/*
 * Initialise the required types from the standard library enum module.  Return
 * a negative value and raise an exception if there is an error.
 */
static int init_enum_module_types(sipSipModuleState *sms)
{
    /* Check if it has already been done. */
    if (sms->enum_enum_type != NULL)
        return 0;

#if defined(SIP_CONFIGURATION_PyEnums)
    /* Get the builtin types. */
    PyObject *builtins = PyEval_GetFrameBuiltins();
    if (builtins == NULL)
        return -1;

    sms->builtin_int_type = PyDict_GetItemString(builtins, "int");
    sms->builtin_object_type = PyDict_GetItemString(builtins, "object");

    Py_DECREF(builtins);

    if (sms->builtin_int_type == NULL || sms->builtin_object_type == NULL)
    {
        Py_CLEAR(sms->builtin_int_type);
        Py_CLEAR(sms->builtin_object_type);

        return -1;
    }
#endif

    /* Get the enum types. */
    PyObject *enum_module = PyImport_ImportModule("enum");
    if (enum_module == NULL)
        return -1;

    sms->enum_enum_type = PyObject_GetAttrString(enum_module, "Enum");
    sms->enum_int_enum_type = PyObject_GetAttrString(enum_module, "IntEnum");
#if defined(SIP_CONFIGURATION_PyEnums)
    sms->enum_flag_type = PyObject_GetAttrString(enum_module, "Flag");
    sms->enum_int_flag_type = PyObject_GetAttrString(enum_module, "IntFlag");
#endif

    Py_DECREF(enum_module);

    if (sms->enum_enum_type == NULL || sms->enum_int_enum_type == NULL
#if defined(SIP_CONFIGURATION_PyEnums)
        || sms->enum_flag_type == NULL || sms->enum_int_flag_type == NULL
#endif
        )
    {
        Py_CLEAR(sms->enum_enum_type);
        Py_CLEAR(sms->enum_int_enum_type);
#if defined(SIP_CONFIGURATION_PyEnums)
        Py_CLEAR(sms->enum_flag_type);
        Py_CLEAR(sms->enum_int_flag_type);
#endif

        return -1;
    }

    return 0;
}


#if defined(SIP_CONFIGURATION_PyEnums)
/*
 * The bulk of the implementation of _missing_ that handles missing members.
 */
static PyObject *missing(PyObject *cls, PyObject *value, int int_enum)
{
    sipSipModuleState *sms = sip_get_sip_module_state_from_type(
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


/*
 * Raise an exception relating to an invalid type ID.
 */
static void raise_internal_error(sipTypeID type_id)
{
    PyErr_Format(PyExc_SystemError, "unsupported enum type ID: 0x%04x",
            type_id);
}
