/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the callable type.
 *
 * Copyright (c) 2026 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_CALLABLE_H
#define _SIP_CALLABLE_H

#include <Python.h>

#include "sip.h"
#include "sip_decls.h"


#ifdef __cplusplus
extern "C" {
#endif

PyObject *sipCallable_New(sipSipModuleState *sms,
        const sipCallableSpec *c_spec, PyObject *defining_module,
        PyObject *self, const sipTypeSpec *extending_ts);
int sip_callable_init(PyObject *module, sipSipModuleState *sms);

#ifdef __cplusplus
}
#endif

#endif
