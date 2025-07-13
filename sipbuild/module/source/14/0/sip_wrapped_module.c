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

#include "sip_container.h"
#include "sip_core.h"
#include "sip_enum.h"
#include "sip_module.h"
#include "sip_module_methods.h"


/* Forward references. */
static int add_license(PyObject *dict, const sipLicenseDef *lc);


/* The wrapped module's clear slot. */
int sip_api_wrapped_module_clear(sipWrappedModuleState *wms)
{
    int i;

    /* Clear the wrapped types. */
    for (i = 0; i < wms->wrapped_module_def->wm_nr_types; i++)
        Py_CLEAR(wms->py_types[i]);

    Py_CLEAR(wms->imported_modules);
    Py_CLEAR(wms->sip_module);
    Py_CLEAR(wms->wrapped_module_name);

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
        wms->wrapped_module_def->wm_delayeddtors(wms->delayed_dtors_list);

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
int sip_api_wrapped_module_init(PyObject *wmod, const sipWrappedModuleDef *wmd,
        PyObject *sip_module)
{
    /* Check that we can support it. */
    if (wmd->wm_abi_major != SIP_ABI_MAJOR_VERSION || wmd->wm_abi_minor > SIP_ABI_MINOR_VERSION)
    {
#if SIP_ABI_MINOR_VERSION > 0
        PyErr_Format(PyExc_RuntimeError,
                "the sip module implements ABI v%d.0 to v%d.%d but the %s module requires ABI v%d.%d",
                SIP_ABI_MAJOR_VERSION, SIP_ABI_MAJOR_VERSION,
                SIP_ABI_MINOR_VERSION, PyModule_GetName(wmod),
                wmd->wm_abi_major, wmd->wm_abi_minor);
#else
        PyErr_Format(PyExc_RuntimeError,
                "the sip module implements ABI v%d.0 but the %s module requires ABI v%d.%d",
                SIP_ABI_MAJOR_VERSION, PyModule_GetName(wmod),
                wmd->wm_abi_major, wmd->wm_abi_minor);
#endif

        return -1;
    }

    if (wmd->wm_sip_configuration != SIP_CONFIGURATION)
    {
        PyErr_Format(PyExc_RuntimeError,
                "the sip module has a configuration of 0x%04x but the %s module requires 0x%04x",
                SIP_CONFIGURATION, PyModule_GetName(wmod),
                wmd->wm_sip_configuration);

        return -1;
    }

    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    wms->sip_api = &sip_api;
#if _SIP_MODULE_SHARED
    wms->sip_module = sip_module;
    wms->sip_module_state = (sipSipModuleState *)PyModule_GetState(sip_module);
#else
    wms->sip_module = wmod;
    Py_INCREF(wmod);

    wms->sip_module_state = sip_api_malloc(sizeof (sipSipModuleState));

    if (sip_sip_module_init(wms->sip_module_state, wmod) < 0)
        return -1;
#endif
    wms->wrapped_module_def = wmd;
    wms->wrapped_module_name = PyModule_GetNameObject(wmod);

    /* Add the SIP version number. */
    if (PyModule_AddIntMacro(wmod, SIP_VERSION) < 0)
        return -1;

    if (PyModule_AddStringMacro(wmod, SIP_VERSION_STR) < 0)
        return -1;

    /* Add the SIP ABI version number. */
    const long abi_version = (SIP_ABI_MAJOR_VERSION << 16) +
            (SIP_ABI_MINOR_VERSION << 8) +
            SIP_MODULE_PATCH_VERSION;

    if (PyModule_AddIntConstant(wmod, "SIP_ABI_VERSION", abi_version) < 0)
        return -1;

    /* Add the module's methods. */
    if (PyModule_AddFunctions(wmod, sipModuleMethods) < 0)
        return -1;

    /* Allocate the space for any wrapped type type objects. */
    if (wmd->wm_nr_types > 0 && (wms->py_types = PyMem_Calloc(wmd->wm_nr_types, sizeof (PyTypeObject *))) == NULL)
            return -1;

    /* Import any required wrapped modules. */
    if (wmd->wm_nr_imports > 0)
    {
        if ((wms->imported_modules = PyList_New(wmd->wm_nr_imports)) == NULL)
            return -1;

        Py_ssize_t i;

        for (i = 0; i < wmd->wm_nr_imports; i++)
        {
            PyObject *im_mod = PyImport_ImportModule(wmd->wm_imports[i]);
            if (im_mod == NULL)
                return -1;

            PyList_SET_ITEM(wms->imported_modules, i, im_mod);
        }
    }

    /* Add it to the list of wrapped modules. */
    sipSipModuleState *sms = wms->sip_module_state;

    if (sip_append_py_object_to_list(&sms->module_list, wmod) < 0)
        return -1;

#if 0
// Need to specify the enums in a different way.
#if defined(SIP_CONFIGURATION_PyEnums)
    const sipIntInstanceDef *next_int = wmd->wm_instances.id_int;
#endif
#endif

    /* Create the module's types. */
    PyObject *wmod_dict = PyModule_GetDict(wmod);
    int i;

    for (i = 0; i < wmd->wm_nr_types; ++i)
    {
        const sipTypeDef *td = wmd->wm_types[i];

        /* Skip external classes. */
        if (td == NULL)
             continue;

        /* Skip if it is already initialised. */
        PyTypeObject *py_type = wms->py_types[i];

        if (py_type != NULL)
            continue;

#if defined(SIP_CONFIGURATION_PyEnums)
#if 0
// Need to specify the enums in a different way.
        if (sipTypeIsEnum(td))
        {
            const sipEnumTypeDef *etd = (const sipEnumTypeDef *)td;

            if (etd->etd_scope < 0 && (py_type = sip_enum_create_py_enum(wms, etd, &next_int, wmod_dict)) == NULL)
                return -1;
        }
#endif
#endif
#if defined(SIP_CONFIGURATION_CustomEnums)
        if (sipTypeIsEnum(td) || sipTypeIsScopedEnum(td))
        {
            const sipEnumTypeDef *etd = (const sipEnumTypeDef *)td;

            if ((py_type = sip_enum_create_custom_enum(sms, wmd, etd, i, wmod_dict)) == NULL)
                return -1;

            /*
             * Register the enum pickler for nested enums (non-nested enums
             * don't need special treatment).
             */
            if (sipTypeIsEnum(td) && etd->etd_scope >= 0)
            {
                static PyMethodDef md = {
                    "_pickle_enum", (PyCFunction)sip_enum_pickle_custom_enum, METH_METHOD|METH_FASTCALL|METH_KEYWORDS, NULL
                };

                if (set_reduce(py_type, &md) < 0)
                    return -1;
            }
        }
#endif
        else if (sipTypeIsMapped(td))
        {
            const sipMappedTypeDef *mtd = (const sipMappedTypeDef *)td;

            /* If there is a name then we need a namespace. */
            if (mtd->mtd_container.cod_name != NULL && (py_type = sip_create_mapped_type(sms, wmd, mtd, wmod_dict)) == NULL)
                    return -1;
        }
        else
        {
            const sipClassTypeDef *ctd = (const sipClassTypeDef *)td;

            /* See if this is a namespace extender. */
            if (ctd->ctd_container.cod_name == NULL)
            {
#if 0
                const sipTypeDef *real_nspace;
                sipClassTypeDef **last;

                real_nspace = sip_get_type_def(wms, ctd->ctd_container.cod_scope);

                /* Append this type to the real one. */
                last = &((const sipClassTypeDef *)real_nspace)->ctd_nsextender;

                while (*last != NULL)
                    last = &(*last)->ctd_nsextender;

                *last = ctd;

                /*
                 * Save the real namespace type so that it is the correct scope
                 * for any enums or classes defined in this module.
                 */
                wmd->wm_types[i] = real_nspace;
#endif
            }
            else if ((py_type = sip_create_class_type(sms, wmd, ctd, wmod_dict)) != NULL)
            {
                return -1;
            }
        }

        /* Save the new type. */
        wms->py_types[i] = py_type;
    }

#if defined(SIP_CONFIGURATION_PyEnums)
#if 0
// Need to do this a different way if still needed.  Anon enums?
    /* Add any ints that aren't name enum members. */
    if (next_int != NULL)
        if (sip_container_add_int_instances(wmod_dict, next_int) < 0)
            return -1;
#endif
#endif

#if 0
    /* Append any initialiser extenders to the relevant classes. */
    if (wmd->wm_init_extend != NULL)
    {
        sipInitExtenderDef *ie = wmd->wm_init_extend;

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
    if (wmd->wm_convertors != NULL)
    {
        sipSubClassConvertorDef *scc = wmd->wm_convertors;

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

    for (emd = wmd->wm_enum_members, i = 0; i < wmd->wm_nr_enum_members; ++i, ++emd)
    {
        const sipTypeDef *etd = wmd->wm_types[emd->em_enum];
        PyObject *mo;

        if (sipTypeIsScopedEnum(etd))
            continue;

        mo = sip_enum_convert_from_enum(sms, emd->em_val, etd);

        if (sip_dict_set_and_discard(wmod_dict, emd->em_name, mo) < 0)
            return -1;
    }
#endif

#if 0
// This is no longer needed but are any non-static values that need to be added?
    /* Add any global static instances. */
    if (sip_container_add_instances(wms, wmod_dict, &wmd->wm_instances) < 0)
        return -1;
#endif

    /* Add any license. */
    if (wmd->wm_license != NULL && add_license(wmod_dict, wmd->wm_license) < 0)
        return -1;

#if 0
    /* See if the new module satisfies any outstanding external types. */
    for (i = 0; i < PyList_GET_SIZE(sms->module_list); i++)
    {
        PyObject *mod = PyList_GET_ITEM(sms->module_list, i);

        if (mod == wmod)
            continue;

        sipWrappedModuleState *ms = (sipWrappedModuleState *)PyModule_GetState(
                mod);
        const sipWrappedModuleDef *md = ms->wrapped_module_def;

        if (md->wm_external == NULL)
            continue;

        sipExternalTypeDef *etd;

        for (etd = md->wm_external; etd->et_nr >= 0; ++etd)
        {
            if (etd->et_name == NULL)
                continue;

            for (i = 0; i < wmd->wm_nr_types; ++i)
            {
                sipTypeDef *td = wmd->wm_types[i];

                if (td != NULL && sipTypeIsClass(td))
                {
                    const char *pyname = &((const sipClassTypeDef *)td)->ctd_container.cod_name, td);

                    if (strcmp(etd->et_name, pyname) == 0)
                    {
                        md->wm_types[etd->et_nr] = td;
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
    for (i = 0; i < wms->wrapped_module_def->wm_nr_types; i++)
        Py_VISIT(wms->py_types[i]);

    Py_VISIT(wms->imported_modules);
    Py_VISIT(wms->sip_module);
    Py_VISIT(wms->wrapped_module_name);

#if !_SIP_MODULE_SHARED
    sip_sip_module_traverse(wms->sip_module_state, visit, arg);
#endif

    return 0;
}


/*
 * Add a license to a dictionary.
 */
static int add_license(PyObject *dict, const sipLicenseDef *lc)
{
    int rc;
    PyObject *ldict, *proxy, *o;

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

    /* Create a read-only proxy. */
    if ((proxy = PyDictProxy_New(ldict)) == NULL)
        goto deldict;

    Py_DECREF(ldict);

    rc = PyDict_SetItemString(dict, "__license__", proxy);
    Py_DECREF(proxy);

    return rc;

deldict:
    Py_DECREF(ldict);

    return -1;
}
