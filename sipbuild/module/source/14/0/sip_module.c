/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The core sip module code.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip.h"
#include "sip_core.h"


/* The module-specific state. */
typedef struct {
} module_state;


/* Forward declarations. */
static int module_exec(PyObject *module);


#if _SIP_MODULE_SHARED
/*
 * The sip module initialisation function.
 */
#if defined(SIP_STATIC_MODULE)
PyObject *_SIP_MODULE_ENTRY(void)
#else
PyMODINIT_FUNC _SIP_MODULE_ENTRY(void)
#endif
{
    static PyModuleDef_Slot module_slots[] = {
        {Py_mod_exec, module_exec},
#if PY_VERSION_HEX >= 0x030c0000
        {Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED},
#endif
#if PY_VERSION_HEX >= 0x030d0000
        {Py_mod_gil, Py_MOD_GIL_USED},
#endif
        {0, NULL},
    };

    static PyModuleDef module_def = {
        .m_base = PyModuleDef_HEAD_INIT,
        .m_name = _SIP_MODULE_FQ_NAME,
        .m_doc = PyDoc_STR("Bindings related utilities"),
        .m_size = sizeof (module_state),
        //.m_methods = module_methods,
        .m_slots = module_slots,
        //.m_traverse = module_traverse,
        //.m_clear = module_clear,
        //.m_free = module_free,
    };

    return PyModuleDef_Init(&module_def);
}
#endif


/*
 * Implement the exec phase of the module initialisation.
 */
static int module_exec(PyObject *module)
{
    PyObject *module_dict = PyModule_GetDict(module);

    /* Initialise the module dictionary and static variables. */
    const sipAPIDef *api = sip_init_library(module_dict);

    if (api == NULL)
        return -1;

    /* Publish the SIP API. */
    PyObject *api_obj = PyCapsule_New((void *)api,
            _SIP_MODULE_FQ_NAME "._C_API", NULL);

    if (sip_dict_set_and_discard(module_dict, "_C_API", api_obj) < 0)
        return -1;

    return 0;
}
