/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The implementation of the wrapped module support.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip_wrapped_module.h"

#include "sip_core.h"
#include "sip_enum.h"
#include "sip_module.h"
#include "sip_module_methods.h"


/* Forward references. */
static int add_license(PyObject *w_mod, const sipLicenseDef *lc);


/* The wrapped module's clear slot. */
int sip_api_wrapped_module_clear(sipWrappedModuleState *wms)
{
    int i;

    /* Clear the wrapped types. */
    for (i = 0; i < wms->wrapped_module_def->nr_type_defs; i++)
        Py_CLEAR(wms->py_types[i]);

    Py_CLEAR(wms->extra_refs);
    Py_CLEAR(wms->imported_modules);
    Py_CLEAR(wms->sip_module);

#if !_SIP_MODULE_SHARED
    sip_sip_module_clear(wms->sip_module_state);
#endif

    return 0;
}


/* The wrapped module's free slot. */
void sip_api_wrapped_module_free(sipWrappedModuleState *wms)
{
    /* Handle any delayed dtors and free the list. */
    if (wms->delayed_dtors_list != NULL)
    {
        /* Call the handler for the list. */
        wms->wrapped_module_def->delayeddtors(wms->delayed_dtors_list);

        /* Free the list. */
        do
        {
            sipDelayedDtor *dd = wms->delayed_dtors_list;

            wms->delayed_dtors_list = dd->dd_next;
            sip_api_free(dd);
        }
        while (wms->delayed_dtors_list != NULL);
    }

    /* Clear all the Python references. */
    sip_api_wrapped_module_clear(wms);

    /* Free the additional memory related to Python types. */
    if (wms->py_types != NULL)
        PyMem_Free(wms->py_types);

#if !_SIP_MODULE_SHARED
    sip_sip_module_free(wms->sip_module_state);
    sip_api_free(wms->sip_module_state);
#endif

}


/*
 * Initialise a wrapped module.
 */
// TODO There are lots of leaks if this fails in any way.
int sip_api_wrapped_module_init(PyObject *w_mod,
        const sipWrappedModuleDef *wmd, PyObject *sip_module)
{
    /* Check that we can support it. */
    if (wmd->abi_major != SIP_ABI_MAJOR_VERSION || wmd->abi_minor > SIP_ABI_MINOR_VERSION)
    {
#if SIP_ABI_MINOR_VERSION > 0
        PyErr_Format(PyExc_RuntimeError,
                "the sip module implements ABI v%d.0 to v%d.%d but the %s module requires ABI v%d.%d",
                SIP_ABI_MAJOR_VERSION, SIP_ABI_MAJOR_VERSION,
                SIP_ABI_MINOR_VERSION, PyModule_GetName(w_mod),
                wmd->abi_major, wmd->abi_minor);
#else
        PyErr_Format(PyExc_RuntimeError,
                "the sip module implements ABI v%d.0 but the %s module requires ABI v%d.%d",
                SIP_ABI_MAJOR_VERSION, PyModule_GetName(w_mod),
                wmd->abi_major, wmd->abi_minor);
#endif

        return -1;
    }

    if (wmd->sip_configuration != SIP_CONFIGURATION)
    {
        PyErr_Format(PyExc_RuntimeError,
                "the sip module has a configuration of 0x%04x but the %s module requires 0x%04x",
                SIP_CONFIGURATION, PyModule_GetName(w_mod),
                wmd->sip_configuration);

        return -1;
    }

    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            w_mod);

    wms->sip_api = &sip_api;
#if _SIP_MODULE_SHARED
    wms->sip_module = sip_module;
    wms->sip_module_state = (sipSipModuleState *)PyModule_GetState(sip_module);
#else
    wms->sip_module = w_mod;
    Py_INCREF(w_mod);

    wms->sip_module_state = sip_api_malloc(sizeof (sipSipModuleState));

    if (sip_sip_module_init(wms->sip_module_state, w_mod) < 0)
        return -1;
#endif
    wms->wrapped_module = w_mod;
    wms->wrapped_module_def = wmd;

    /* Update the new module's super-type. */
    PyObject *class_s = PyUnicode_InternFromString("__class__");
    if (class_s == NULL)
        return -1;

    if (PyObject_SetAttr(w_mod, class_s, (PyObject *)wms->sip_module_state->module_wrapper_type) < 0)
    {
        Py_DECREF(class_s);
        return -1;
    }

    Py_DECREF(class_s);

    /* Add the SIP version number. */
    if (PyModule_AddIntMacro(w_mod, SIP_VERSION) < 0)
        return -1;

    if (PyModule_AddStringMacro(w_mod, SIP_VERSION_STR) < 0)
        return -1;

    /* Add the SIP ABI version number. */
    const long abi_version = (SIP_ABI_MAJOR_VERSION << 16) +
            (SIP_ABI_MINOR_VERSION << 8) +
            SIP_MODULE_PATCH_VERSION;

    if (PyModule_AddIntConstant(w_mod, "SIP_ABI_VERSION", abi_version) < 0)
        return -1;

    /* Add the module's methods. */
    if (PyModule_AddFunctions(w_mod, sipModuleMethods) < 0)
        return -1;

    /* Allocate the space for any wrapped type type objects. */
    if (wmd->nr_type_defs > 0 && (wms->py_types = PyMem_Calloc(wmd->nr_type_defs, sizeof (PyTypeObject *))) == NULL)
            return -1;

    /* Import any required wrapped modules. */
    if (wmd->nr_imports > 0)
    {
        if ((wms->imported_modules = PyList_New(wmd->nr_imports)) == NULL)
            return -1;

        Py_ssize_t i;

        for (i = 0; i < wmd->nr_imports; i++)
        {
            PyObject *im_mod = PyImport_ImportModule(wmd->imports[i]);
            if (im_mod == NULL)
                return -1;

            PyList_SET_ITEM(wms->imported_modules, i, im_mod);
        }
    }

    /* Add it to the list of wrapped modules. */
    sipSipModuleState *sms = wms->sip_module_state;

    if (sip_append_py_object_to_list(&sms->module_list, w_mod) < 0)
        return -1;

#if 0
// Need to specify the enums in a different way.
#if defined(SIP_CONFIGURATION_PyEnums)
    const sipIntInstanceDef *next_int = wmd->instances.id_int;
#endif
#endif

#if defined(SIP_CONFIGURATION_PyEnums)
#if 0
// Need to do this a different way if still needed.  Anon enums?
    /* Add any ints that aren't name enum members. */
    if (next_int != NULL)
        if (sip_container_add_int_instances(w_mod_dict, next_int) < 0)
            return -1;
#endif
#endif

#if 0
    /* Append any initialiser extenders to the relevant classes. */
    if (wmd->init_extend != NULL)
    {
        sipInitExtenderDef *ie = wmd->init_extend;

        while (ie->ie_extender != NULL)
        {
            const sipTypeDef *td = sip_get_type_def(wms, ie->ie_class);
            sipWrapperType *wt = (sipWrapperType *)sipTypeAsPyTypeObject(td);

            ie->ie_next = wt->wt_iextend;
            wt->wt_iextend = ie;

            ++ie;
        }
    }
#endif

#if 0
    /* Set the base class object for any sub-class convertors. */
    if (wmd->convertors != NULL)
    {
        sipSubClassConvertorDef *scc = wmd->convertors;

        while (scc->scc_convertor != NULL)
        {
            scc->scc_basetype = sip_get_type_def(wms, scc->scc_base);

            ++scc;
        }
    }
#endif

#if defined(SIP_CONFIGURATION_CustomEnums)
    /* Create the module's enum members. */
    sipEnumMemberDef *emd;

    for (emd = wmd->enum_members, i = 0; i < wmd->nr_enum_members; ++i, ++emd)
    {
        const sipTypeDef *etd = wmd->types[emd->em_enum];
        PyObject *mo;

        if (sipTypeIsScopedEnum(etd))
            continue;

        mo = sip_enum_convert_from_enum(sms, emd->em_val, etd);

        if (sip_dict_set_and_discard(w_mod_dict, emd->em_name, mo) < 0)
            return -1;
    }
#endif

#if 0
// This is no longer needed but are any non-static values that need to be added?
    /* Add any global static instances. */
    if (sip_container_add_instances(wms, w_mod_dict, &wmd->instances) < 0)
        return -1;
#endif

    /* Add any license. */
    if (wmd->license != NULL && add_license(w_mod, wmd->license) < 0)
        return -1;

#if 0
    /* See if the new module satisfies any outstanding external types. */
    for (i = 0; i < PyList_GET_SIZE(sms->module_list); i++)
    {
        PyObject *mod = PyList_GET_ITEM(sms->module_list, i);

        if (mod == w_mod)
            continue;

        sipWrappedModuleState *ms = (sipWrappedModuleState *)PyModule_GetState(
                mod);
        const sipWrappedModuleDef *md = ms->wrapped_module_def;

        if (md->external == NULL)
            continue;

        sipExternalTypeDef *etd;

        for (etd = md->external; etd->et_nr >= 0; ++etd)
        {
            if (etd->et_name == NULL)
                continue;

            for (i = 0; i < wmd->nr_type_defs; ++i)
            {
                sipTypeDef *td = wmd->type_defs[i];

                if (td != NULL && sipTypeIsClass(td))
                {
                    const char *pyname = &((const sipClassTypeDef *)td)->ctd_container.cod_name, td);

                    if (strcmp(etd->et_name, pyname) == 0)
                    {
                        md->type_defs[etd->et_nr] = td;
                        etd->et_name = NULL;

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
int sip_api_wrapped_module_traverse(sipWrappedModuleState *wms,
        visitproc visit, void *arg)
{
    int i;

    /* Visit the types. */
    for (i = 0; i < wms->wrapped_module_def->nr_type_defs; i++)
        Py_VISIT(wms->py_types[i]);

    Py_VISIT(wms->extra_refs);
    Py_VISIT(wms->imported_modules);
    Py_VISIT(wms->sip_module);

#if !_SIP_MODULE_SHARED
    sip_sip_module_traverse(wms->sip_module_state, visit, arg);
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
    if (lc->lc_type == NULL)
        goto deldict;

    if ((o = PyUnicode_FromString(lc->lc_type)) == NULL)
        goto deldict;

    rc = PyDict_SetItemString(ldict, "Type", o);
    Py_DECREF(o);

    if (rc < 0)
        goto deldict;

    if (lc->lc_licensee != NULL)
    {
        if ((o = PyUnicode_FromString(lc->lc_licensee)) == NULL)
            goto deldict;

        rc = PyDict_SetItemString(ldict, "Licensee", o);
        Py_DECREF(o);

        if (rc < 0)
            goto deldict;
    }

    if (lc->lc_timestamp != NULL)
    {
        if ((o = PyUnicode_FromString(lc->lc_timestamp)) == NULL)
            goto deldict;

        rc = PyDict_SetItemString(ldict, "Timestamp", o);
        Py_DECREF(o);

        if (rc < 0)
            goto deldict;
    }

    if (lc->lc_signature != NULL)
    {
        if ((o = PyUnicode_FromString(lc->lc_signature)) == NULL)
            goto deldict;

        rc = PyDict_SetItemString(ldict, "Signature", o);
        Py_DECREF(o);

        if (rc < 0)
            goto deldict;
    }

    /* Create and save a read-only proxy. */
#if PY_MAJOR_VERSION >= 0x030d0000
    rc = PyModule_Add(w_mod, "__license__", PyDictProxy_New(ldict));
#else
    PyObject *proxy = PyDictProxy_New(ldict);
#if PY_MAJOR_VERSION >= 0x030a0000
    rc = PyModule_AddObjectRef(w_mod, "__license__", proxy);
    Py_XDECREF(proxy);
#else
    if (proxy == NULL)
        goto deldict;

    rc = PyModule_AddObject(w_mod, "__license__", proxy);
    if (rc < 0)
        Py_DECREF(proxy);
#endif
#endif

    Py_DECREF(ldict);
    return rc;

deldict:
    Py_DECREF(ldict);
    return -1;
}
