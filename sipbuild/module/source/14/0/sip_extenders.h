/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * This file defines the API for the extender support.
 *
 * Copyright (c) 2026 Phil Thompson <phil@riverbankcomputing.com>
 */


#ifndef _SIP_EXTENDERS_H
#define _SIP_EXTENDERS_H

#include <Python.h>

#include "sip.h"
#include "sip_decls.h"


#ifdef __cplusplus
extern "C" {
#endif

PyObject *sip_extend(sipModuleState *ms, PyObject **p_state_p, PyObject *self,
        PyObject *const *args, Py_ssize_t nr_args, PyObject *kwd_names,
        const sipTypeSpec *extending_ts, const char *name);
int sip_get_extension_attribute(sipModuleState *ms,
        const sipTypeSpec *extending_ts, const char *name,
        sipModuleState **x_ms_p, const sipAttrSpec **x_attr_spec_p);

#ifdef __cplusplus
}
#endif

#endif
