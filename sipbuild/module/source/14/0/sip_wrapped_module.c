/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The implementation of the wrapped module support.
 *
 * Copyright (c) 2026 Phil Thompson <phil@riverbankcomputing.com>
 */


#include <Python.h>

#include "sip_wrapped_module.h"

#include "sip_callable.h"
#include "sip_core.h"
#include "sip_enum.h"
#include "sip_module.h"
#include "sip_module_methods.h"


/* Forward references. */
static int add_license(PyObject *mod, const sipLicenseDef *lc);
static int import_module(const sipImportedModuleSpec *ims,
        sipImportedModule *im);
static void module_clear(sipModuleState *ms);


/* The wrapped module's clear slot. */
int sip_api_module_clear(PyObject *mod)
{
    module_clear(sip_get_module_state(mod));

    return 0;
}


/* The wrapped module's free slot. */
void sip_api_module_free(void *mod_ptr)
{
    sipModuleState *ms = sip_get_module_state((PyObject *)mod_ptr);

    /* Handle any delayed dtors and free the list. */
    if (ms->delayed_dtors_list != NULL)
    {
        /* Call the handler for the list. */
        ms->module_spec->delayeddtors(ms->delayed_dtors_list);

        /* Free the list. */
        do
        {
            sipDelayedDtor *dd = ms->delayed_dtors_list;

            ms->delayed_dtors_list = dd->next;
            sip_api_free(dd);
        }
        while (ms->delayed_dtors_list != NULL);
    }

    /* Clear all the Python references. */
    module_clear(ms);

    /* Free the additional memory related to Python types. */
    if (ms->py_types != NULL)
        PyMem_Free(ms->py_types);

    /* Free the additional memory related to imported modules. */
    if (ms->imported_modules != NULL)
    {
        sipModuleNr mi;

        for (mi = 0; mi < ms->module_spec->nr_import_specs; mi++)
            if (ms->imported_modules[mi].type_nr_map != NULL)
                PyMem_Free(ms->imported_modules[mi].type_nr_map);

        PyMem_Free(ms->imported_modules);
    }

    /* Free any pending extenders. */
    while (ms->pending_extenders != NULL)
    {
        sipPendingExtender *pe = ms->pending_extenders;

        ms->pending_extenders = pe->next;
        sip_api_free(pe);
    }

#if !_SIP_MODULE_SHARED
    sip_sip_module_free(ms->sip_module_state);
    sip_api_free(ms->sip_module_state);
#endif
}


/*
 * The execute phase of a wrapped module initialisation.
 */
// TODO There are lots of leaks if this fails in any way.  However the free
// slot will be called so make sure things are in a state where it can tidy up
// after any failure.
int sip_api_module_exec(PyObject *mod, const sipModuleSpec *m_spec)
{
    /* Check that we can support it. */
    if (m_spec->abi_major != SIP_ABI_MAJOR_VERSION || m_spec->abi_minor > SIP_ABI_MINOR_VERSION)
    {
#if SIP_ABI_MINOR_VERSION > 0
        PyErr_Format(PyExc_RuntimeError,
                "the sip module implements ABI v%d.0 to v%d.%d but the %s module requires ABI v%d.%d",
                SIP_ABI_MAJOR_VERSION, SIP_ABI_MAJOR_VERSION,
                SIP_ABI_MINOR_VERSION, PyModule_GetName(mod),
                m_spec->abi_major, m_spec->abi_minor);
#else
        PyErr_Format(PyExc_RuntimeError,
                "the sip module implements ABI v%d.0 but the %s module requires ABI v%d.%d",
                SIP_ABI_MAJOR_VERSION, PyModule_GetName(mod),
                m_spec->abi_major, m_spec->abi_minor);
#endif

        return -1;
    }

    if (m_spec->sip_configuration != SIP_CONFIGURATION)
    {
        PyErr_Format(PyExc_RuntimeError,
                "the sip module has a configuration of 0x%04x but the %s module requires 0x%04x",
                SIP_CONFIGURATION, PyModule_GetName(mod),
                m_spec->sip_configuration);

        return -1;
    }

    sipModuleState *ms = sip_get_module_state(mod);

#if _SIP_MODULE_SHARED
    ms->sip_module = PyImport_ImportModule(_SIP_MODULE_FQ_NAME);
    if (ms->sip_module == NULL)
        return -1;

    ms->sip_module_state = sip_get_sip_module_state(ms->sip_module);
#else
    ms->sip_module = Py_NewRef(mod);
    ms->sip_module_state = sip_api_malloc(sizeof (sipSipModuleState));

    if (sip_sip_module_init(ms->sip_module_state, mod) < 0)
        return -1;
#endif
    ms->wrapped_module = mod;
    ms->module_spec = m_spec;

    sipSipModuleState *sms = ms->sip_module_state;

    /* Update the new module's super-type. */
    PyObject *class_s = PyUnicode_InternFromString("__class__");
    if (class_s == NULL)
        return -1;

    if (PyObject_SetAttr(mod, class_s, (PyObject *)sms->module_wrapper_type) < 0)
    {
        Py_DECREF(class_s);
        return -1;
    }

    Py_DECREF(class_s);

    /* Add the SIP version number. */
    if (PyModule_AddIntMacro(mod, SIP_VERSION) < 0)
        return -1;

    if (PyModule_AddStringMacro(mod, SIP_VERSION_STR) < 0)
        return -1;

    /* Add the SIP ABI version number. */
    const long abi_version = (SIP_ABI_MAJOR_VERSION << 16) +
            (SIP_ABI_MINOR_VERSION << 8) +
            SIP_MODULE_PATCH_VERSION;

    if (PyModule_AddIntConstant(mod, "SIP_ABI_VERSION", abi_version) < 0)
        return -1;

#if !_SIP_MODULE_SHARED
    /* Add the sip module's methods. */
    if (PyModule_AddFunctions(mod, sipSipModuleMethods) < 0)
        return -1;
#endif

    /* Add any module callables. */
    if (m_spec->callables != NULL)
    {
        const sipCallableSpec *c_spec = m_spec->callables;

        while (c_spec->name != NULL)
        {
            PyObject *callable = sipCallable_New(sms, c_spec, mod);
            int rc = PyModule_AddObjectRef(mod, c_spec->name, callable);
            Py_XDECREF(callable);

            if (rc < 0)
                return -1;

            c_spec++;
        }
    }

    /* Allocate the space for any wrapped type type objects. */
    if (m_spec->nr_type_specs > 0 && (ms->py_types = PyMem_Calloc(m_spec->nr_type_specs, sizeof (PyTypeObject *))) == NULL)
        return -1;

    /* Import any required wrapped modules. */
    if (m_spec->nr_import_specs > 0)
    {
        if ((ms->imported_modules = PyMem_Calloc(m_spec->nr_import_specs, sizeof (sipImportedModule))) == NULL)
            return -1;

        sipModuleNr mi;

        for (mi = 0; mi < m_spec->nr_import_specs; mi++)
            if (import_module(&m_spec->import_specs[mi], &ms->imported_modules[mi]) < 0)
                return -1;
    }

    /* Add it to the list of wrapped modules. */
    if (sip_append_py_object_to_list(&sms->module_list, mod) < 0)
        return -1;

#if 0
    /* Append any initialiser extenders to the relevant classes. */
    if (m_spec->init_extend != NULL)
    {
        sipInitExtenderDef *ie = m_spec->init_extend;

        while (ie->extender != NULL)
        {
            const sipTypeSpec *td = sip_get_type_spec(ms, ie->type_id);
            sipWrapperType *wt = (sipWrapperType *)sipTypeAsPyTypeObject(td);

            ie->next = wt->wt_iextend;
            wt->wt_iextend = ie;

            ++ie;
        }
    }
#endif

#if 0
    /* Set the base class object for any sub-class convertors. */
    if (m_spec->convertors != NULL)
    {
        sipSubClassConvertorDef *scc = m_spec->convertors;

        while (scc->convertor != NULL)
        {
            // TODO This can't be in the immutable spec.
            scc->basetype = sip_get_type_spec(ms, scc->base_id);

            ++scc;
        }
    }
#endif

#if 0
// This is no longer needed but are any non-static values that need to be added?
    /* Add any global static instances. */
    if (sip_container_add_instances(ms, mod_dict, &m_spec->instances) < 0)
        return -1;
#endif

    /* Add any license. */
    if (m_spec->license != NULL && add_license(mod, m_spec->license) < 0)
        return -1;

    /* See if this module contains any class extenders. */
    sipTypeNr ti;

    for (ti = 0; ti < m_spec->nr_type_specs; ti++)
    {
        const sipTypeSpec *ts = m_spec->type_specs[ti];

        if (ts->cpp_name != NULL)
            continue;

        if (!sipTypeIsClass(ts) && !sipTypeIsNamespace(ts))
            continue;

        sipTypeNr extending_type_nr;
        sipModuleState *extending_ms = sip_resolve_type_id(ms,
                ((const sipClassTypeSpec *)ts)->container.scope_id, &extending_type_nr);
        assert(extending_ms != NULL);

        PyTypeObject *py_type = extending_ms->py_types[extending_type_nr];

        if (py_type == NULL)
        {
            /* Register the extender for future use. */
            sipPendingExtender *pe = sip_api_malloc(
                    sizeof (sipPendingExtender));
            if (pe == NULL)
                return -1;

            /*
             * Use a weak reference so that the module containing the extender
             * can be freed if the extender is never used.
             */
            pe->extender_module = PyWeakref_NewRef(mod, NULL);
            assert(pe->extender_module != NULL);

            pe->extending = (const sipClassTypeSpec *)extending_ms->module_spec->type_specs[extending_type_nr];
            pe->extender_id = SIP_TYPE_ID_TYPE_CLASS|SIP_TYPE_ID_LOCAL_MODULE|ti;
            pe->next = extending_ms->pending_extenders;
            extending_ms->pending_extenders = pe;
        }
        else
        {
            /* Create the extender. */
            PyTypeObject *extender_py_type;
            if (sip_get_local_py_type(ms, ti, &extender_py_type) < 0)
                return -1;

            /* Extend the extending type from the extender. */
            if (sip_extend_type(ms->sip_module_state, py_type, extender_py_type) < 0)
                return -1;
        }
    }

#if 0
    /* See if this module satisfies any outstanding external types. */
    for (i = 0; i < PyList_GET_SIZE(sms->module_list); i++)
    {
        PyObject *mod = PyList_GET_ITEM(sms->module_list, i);

        if (mod == mod)
            continue;

        sipModuleState *ms = sip_get_module_state(mod);
        const sipModuleSpec *md = ms->module_spec;

        if (md->external == NULL)
            continue;

        sipExternalTypeDef *etd;

        for (etd = md->external; etd->type_nr >= 0; ++etd)
        {
            if (etd->py_name == NULL)
                continue;

            sipTypeNr tn;

            for (tn = 0; tn < m_spec->nr_type_specs; ++tn)
            {
                sipTypeSpec *td = m_spec->type_specs[tn];

                if (td != NULL && sipTypeIsClass(td))
                {
                    const char *pyname = td->fq_py_name;

                    if (strcmp(etd->py_name, pyname) == 0)
                    {
                        md->type_specs[etd->type_nr] = td;
                        // TODO It's immutable.
                        etd->py_name = NULL;

                        break;
                    }
                }
            }
        }
    }
#endif

    return 0;
}


/* The wrapped module's traverse slot. */
int sip_api_module_traverse(PyObject *mod, visitproc visit, void *arg)
{
    sipModuleState *ms = sip_get_module_state(mod);

    /* Visit the types. */
    sipTypeNr ti;

    for (ti = 0; ti < ms->module_spec->nr_type_specs; ti++)
        Py_VISIT(ms->py_types[ti]);

    /* Visit the imported modules. */
    sipModuleNr mi;

    for (mi = 0; mi < ms->module_spec->nr_import_specs; mi++)
        Py_VISIT(ms->imported_modules[mi].module);

    /* Visit the pending extenders. */
    sipPendingExtender *pe;

    for (pe = ms->pending_extenders; pe != NULL; pe = pe->next)
        Py_VISIT(pe->extender_module);

    Py_VISIT(ms->extra_refs);
    Py_VISIT(ms->sip_module);

#if !_SIP_MODULE_SHARED
    sip_sip_module_traverse(ms->sip_module_state, visit, arg);
#endif

    return 0;
}


/*
 * Add a license to a module.
 */
static int add_license(PyObject *w_mod, const sipLicenseDef *lc)
{
    int rc;
    PyObject *ldict, *o;

    /* We use a dictionary to hold the license information. */
    if ((ldict = PyDict_New()) == NULL)
        return -1;

    /* The license type is compulsory, the rest are optional. */
    if (lc->type == NULL)
        goto deldict;

    if ((o = PyUnicode_FromString(lc->type)) == NULL)
        goto deldict;

    rc = PyDict_SetItemString(ldict, "Type", o);
    Py_DECREF(o);

    if (rc < 0)
        goto deldict;

    if (lc->licensee != NULL)
    {
        if ((o = PyUnicode_FromString(lc->licensee)) == NULL)
            goto deldict;

        rc = PyDict_SetItemString(ldict, "Licensee", o);
        Py_DECREF(o);

        if (rc < 0)
            goto deldict;
    }

    if (lc->timestamp != NULL)
    {
        if ((o = PyUnicode_FromString(lc->timestamp)) == NULL)
            goto deldict;

        rc = PyDict_SetItemString(ldict, "Timestamp", o);
        Py_DECREF(o);

        if (rc < 0)
            goto deldict;
    }

    if (lc->signature != NULL)
    {
        if ((o = PyUnicode_FromString(lc->signature)) == NULL)
            goto deldict;

        rc = PyDict_SetItemString(ldict, "Signature", o);
        Py_DECREF(o);

        if (rc < 0)
            goto deldict;
    }

    /* Create and save a read-only proxy. */
    rc = PyModule_Add(w_mod, "__license__", PyDictProxy_New(ldict));

    Py_DECREF(ldict);
    return rc;

deldict:
    Py_DECREF(ldict);
    return -1;
}


/*
 * Import a module that the module being created requires.
 */
static int import_module(const sipImportedModuleSpec *ims,
        sipImportedModule *im)
{
    /* Import the module. */
    if ((im->module = PyImport_ImportModule(ims->name)) == NULL)
        return -1;

    /* Populate the type number map. */
    if (ims->nr_types > 0)
    {
        if ((im->type_nr_map = PyMem_Calloc(ims->nr_types, sizeof (sipTypeNr))) == NULL)
            return -1;

        sipModuleState *ms = sip_get_module_state(im->module);
        sipTypeNr ti, iti;

        /* We rely on both tables being in the same order. */
        for (iti = ti = 0; ti < ims->nr_types; ti++)
        {
            const char *type_name = ims->type_names[ti];

            while (strcmp(type_name, ms->module_spec->type_specs[iti]->cpp_name) != 0)
                if (++iti >= ms->module_spec->nr_type_specs)
                {
                    PyErr_Format(PyExc_RuntimeError,
                            "unable to find type '%s' in module %s", type_name,
                            ims->name);
                    return -1;
                }

            im->type_nr_map[ti] = iti;
        }
    }

    return 0;
}


/* Clear a wrapped module's Python references. */
static void module_clear(sipModuleState *ms)
{
    /* Clear the wrapped types. */
    sipTypeNr ti;

    for (ti = 0; ti < ms->module_spec->nr_type_specs; ti++)
        Py_CLEAR(ms->py_types[ti]);

    /* Clear the imported modules. */
    sipModuleNr mi;

    for (mi = 0; mi < ms->module_spec->nr_import_specs; mi++)
        Py_CLEAR(ms->imported_modules[ti].module);

    /* Clear the pending extenders. */
    sipPendingExtender *pe;

    for (pe = ms->pending_extenders; pe != NULL; pe = pe->next)
        Py_CLEAR(pe->extender_module);

    Py_CLEAR(ms->extra_refs);
    Py_CLEAR(ms->sip_module);

#if !_SIP_MODULE_SHARED
    sip_sip_module_clear(ms->sip_module_state);
#endif
}
