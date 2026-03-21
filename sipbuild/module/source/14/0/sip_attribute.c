/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The support for attributes.
 *
 * Copyright (c) 2026 Phil Thompson <phil@riverbankcomputing.com>
 */


#include <Python.h>

#include <stdlib.h>
#include <string.h>

#include "sip_attribute.h"

#include "sip_callable.h"
#include "sip_extenders.h"
#include "sip_method_descriptor.h"
#include "sip_module.h"
#include "sip_variable_descriptor.h"
#include "sip_variable.h"


/* Forward declarations. */
static int compare_attribute(const void *key, const void *el);
static PyObject *create_callable(sipModuleState *ms,
        const sipAttrSpec *attr_spec);
static PyObject *create_property(sipModuleState *ms,
        const sipAttrSpec *attr_spec);
static PyObject *descriptor_get(PyObject *attr, PyObject *self);
static PyObject *get_attribute_for_spec(sipModuleState *ms, PyObject *self,
        const sipAttrSpec *attr_spec, const sipTypeSpec *extending_ts);
static PyObject *get_py_type(sipModuleState *ms, const sipAttrSpec *attr_spec);


/*
 * Return the attribute specification for a name or NULL if there was none.
 */
const sipAttrSpec *sip_get_attribute_spec(const char *name,
        const sipAttributesSpec *attrs)
{
    if (attrs->nr_attrs == 0)
        return NULL;

    return (const sipAttrSpec *)bsearch((const void *)name,
            (const void *)attrs->attrs, attrs->nr_attrs, sizeof (sipAttrSpec),
            compare_attribute);
}


/*
 * The getattro handler for modules and containers.
 */
PyObject *sip_mod_con_getattro(sipModuleState *ms, PyObject *self,
        PyObject *name, PyObject *attr_dict,
        const sipAttributesSpec *const attributes,
        const sipAttributesSpec *const static_variables,
        const sipTypeSpec *extending_ts)
{
    const char *utf8_name = PyUnicode_AsUTF8(name);
    const sipAttrSpec *attr_spec;
printf("!!! Attr: %s\n", utf8_name);

    /*
     * The behaviour of static variables is that of a data descriptor and they
     * take precedence over any attributes set by the user.
     */
    attr_spec = sip_get_attribute_spec(utf8_name, static_variables);

    if (attr_spec != NULL)
        return sip_variable_get(ms, self, attr_spec, NULL, NULL);

    /* Get any extension attribute. */
    const sipAttrSpec *x_attr_spec = NULL;
    sipModuleState *x_ms = NULL;

    if (extending_ts != NULL)
    {
        /* See if the extension is a static variable. */
        if (sip_get_extension_attribute(ms, extending_ts, utf8_name, &x_ms, &x_attr_spec) < 0)
            return NULL;

        if (x_attr_spec != NULL && x_attr_spec->name[0] == 'v')
            return sip_variable_get(x_ms, self, x_attr_spec, NULL, NULL);
    }

    /*
     * Revert to the super-class behaviour.  This will pick up any objects
     * already created from wrapped attribute specifications.
     */
    PyObject *attr = Py_TYPE(self)->tp_base->tp_getattro(self, name);
    if (attr != NULL)
        return attr;

    /* See if there is a wrapped attribute. */
    attr_spec = sip_get_attribute_spec(utf8_name, attributes);

    if (attr_spec != NULL)
    {
        attr = get_attribute_for_spec(ms, self, attr_spec, extending_ts);
        if (attr == NULL)
            return NULL;

        /* Save it in the dict. */
        if (PyDict_SetItem(attr_dict, name, attr) < 0)
        {
            Py_DECREF(attr);
            return NULL;
        }

        return descriptor_get(attr, self);
    }

    /* See if the type has been extended. */
    if (x_attr_spec != NULL)
    {
        attr = get_attribute_for_spec(x_ms, self, x_attr_spec, extending_ts);
        if (attr == NULL)
            return NULL;

        return descriptor_get(attr, self);
    }

    /*
     * The exception from the super-class should still be in place if no
     * attribute was found.
     */
    return NULL;
}


/*
 * The setattro handler for modules and containers.
 */
int sip_mod_con_setattro(sipModuleState *ms, PyObject *self, PyObject *name,
        PyObject *value, const sipAttributesSpec *const attributes,
        const sipAttributesSpec *const static_variables,
        const sipTypeSpec *extending_ts)
{
    const char *utf8_name = PyUnicode_AsUTF8(name);

    /* See if there is a wrapped attribute. */
    const sipAttrSpec *attr_spec = sip_get_attribute_spec(utf8_name,
            attributes);

    if (attr_spec == NULL)
        attr_spec = sip_get_attribute_spec(utf8_name, static_variables);

    /*
     * Note that we can't use a real descriptor for class (ie. static)
     * variables because while the type object will look for a data descriptor
     * in the type's dictionary going a get it doesn't when doing a set.
     * Instead it just overwrites the descriptor.
     */

    if (attr_spec != NULL)
        return sip_variable_set(ms, self, value, attr_spec, NULL, NULL);

    /* See if there is an extension. */
    if (extending_ts != NULL)
    {
        const sipAttrSpec *x_attr_spec;
        sipModuleState *x_ms;

        if (sip_get_extension_attribute(ms, extending_ts, utf8_name, &x_ms, &x_attr_spec) < 0)
            return -1;

        if (x_attr_spec != NULL && x_attr_spec->name[0] == 'v')
            return sip_variable_set(x_ms, self, value, x_attr_spec, NULL,
                    NULL);
    }

    return Py_TYPE(self)->tp_base->tp_setattro(self, name, value);
}


/*
 * The bsearch() helper function for searching an attributes table.
 */
static int compare_attribute(const void *key, const void *el)
{
    return strcmp((const char *)key, ((const sipAttrSpec *)el)->name + 1);
}


/*
 * Return a callable or Py_None if there isn't one.
 */
static PyObject *create_callable(sipModuleState *ms,
        const sipAttrSpec *attr_spec)
{
    if (attr_spec == NULL)
        return Py_NewRef(Py_None);

    return sipCallable_New(ms->sip_module_state, attr_spec, ms->wrapped_module,
            NULL, NULL);
}


/*
 * Create and return a Python property.
 */
static PyObject *create_property(sipModuleState *ms,
        const sipAttrSpec *attr_spec)
{
    const sipPropertySpec *ps = attr_spec->spec.property;
    PyObject *prop, *fget, *fset, *doc;

    prop = fget = fset = doc = NULL;

    if ((fget = create_callable(ms, ps->getter)) == NULL)
        goto done;

    if ((fset = create_callable(ms, ps->setter)) == NULL)
        goto done;

    if (attr_spec->docstring == NULL)
    {
        doc = Py_NewRef(Py_None);
    }
    else if ((doc = PyUnicode_FromString(attr_spec->docstring)) == NULL)
    {
        goto done;
    }

    prop = PyObject_CallFunctionObjArgs((PyObject *)&PyProperty_Type, fget,
            fset, Py_None, doc, NULL);

done:
    Py_XDECREF(fget);
    Py_XDECREF(fset);
    Py_XDECREF(doc);

    return prop;
}


/*
 * Implement the get part of the descriptor protocol.
 */
static PyObject *descriptor_get(PyObject *attr, PyObject *self)
{
    descrgetfunc getter = Py_TYPE(attr)->tp_descr_get;

    if (getter != NULL)
        Py_SETREF(attr, getter(attr, self, (PyObject *)Py_TYPE(self)));

    return attr;
}


/*
 * Return a new reference to the Python object for an attribute according to
 * its specification.
 */
static PyObject *get_attribute_for_spec(sipModuleState *ms, PyObject *self,
        const sipAttrSpec *attr_spec, const sipTypeSpec *extending_ts)
{
    /* Clear the super-class AttributeError. */
    PyErr_Clear();

    switch (attr_spec->name[0])
    {
    case 'c':
        return sipMethodDescr_New(ms->sip_module_state, attr_spec,
                ms->wrapped_module, extending_ts);

    case 'i':
        return sipVariableDescr_New(ms->sip_module_state, (PyTypeObject *)self,
                attr_spec);

    case 'm':
        return sipCallable_New(ms->sip_module_state, attr_spec,
                ms->wrapped_module, NULL, NULL);

    case 'p':
        return create_property(ms, attr_spec);

    case 't':
        return get_py_type(ms, attr_spec);
    }

    /* This should never happen. */
    return NULL;
}


/*
 * Return the Python type object for a wrapped type attribute.
 */
static PyObject *get_py_type(sipModuleState *ms, const sipAttrSpec *attr_spec)
{
    /*
     * Note that the type may have been created some time ago (using a type ID
     * from generated code) and this is just the first time it has been
     * accessed as an attribute.
     */
    PyTypeObject *py_type;

    if (sip_get_local_py_type(ms, attr_spec->spec.type_nr, &py_type) < 0)
        return NULL;

    /*
     * The type would be NULL for mapped types with no attributes but that
     * should never happen in this context.
     */
    assert(py_type != NULL);

    return Py_NewRef(py_type);
}
