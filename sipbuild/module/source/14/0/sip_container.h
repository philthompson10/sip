/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the generic container support.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_CONTAINER_H
#define _SIP_CONTAINER_H


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip.h"


#ifdef __cplusplus
extern "C" {
#endif


int sip_container_add_instances(sipWrappedModuleState *wms, PyObject *dict,
        const sipInstancesDef *id);
int sip_container_add_int_instances(PyObject *dict,
        const sipIntInstanceDef *ii);
int sip_container_add_lazy_attrs(sipWrappedModuleState *wms,
        PyTypeObject *py_type, const sipTypeDef *td);
int sip_container_add_type_instance(sipWrappedModuleState *wms, PyObject *dict,
        const char *name, void *cppPtr, sipTypeID type_id, int initflags);


#ifdef __cplusplus
}
#endif

#endif
