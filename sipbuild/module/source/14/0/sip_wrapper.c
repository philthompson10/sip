/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * The implementation of the sip wrapper type.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#include <Python.h>

#include "sip_wrapper.h"

#include "sip_module.h"


/*
 * The type specification.  Note that the slots are implemented by
 * simplewrapper which will deal with the extra requirements of this type.  It
 * is done this way because the correct type can only be determined at
 * run-time.
 */
static PyType_Slot Wrapper_slots[] = {
    {0, NULL}
};

static PyType_Spec Wrapper_TypeSpec = {
    .name = _SIP_MODULE_FQ_NAME ".wrapper",
    .basicsize = sizeof (sipWrapper),
    .flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
    .slots = Wrapper_slots,
};


/*
 * Initialise the wrapper type.
 */
int sip_wrapper_init(PyObject *module, sipSipModuleState *sms)
{
    sms->wrapper_type = (PyTypeObject *)PyType_FromMetaclass(
            sms->wrapper_type_type, module, &Wrapper_TypeSpec,
            (PyObject *)sms->simple_wrapper_type);

    if (sms->wrapper_type == NULL)
        return -1;

    if (PyModule_AddType(module, sms->wrapper_type) < 0)
        return -1;

    return 0;
}
