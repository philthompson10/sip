/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the wrapped module support.
 *
 * Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_WRAPPED_MODULE_H
#define _SIP_WRAPPED_MODULE_H


/* Remove when Python v3.12 is no longer supported. */
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "sip.h"


#ifdef __cplusplus
extern "C" {
#endif


int sip_api_wrapped_module_clear(sipWrappedModuleState *wms);
void sip_api_wrapped_module_free(sipWrappedModuleState *wms);
int sip_api_wrapped_module_traverse(sipWrappedModuleState *wms,
        visitproc visit, void *arg);


#ifdef __cplusplus
}
#endif

#endif
