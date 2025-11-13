# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


def g_composite_module_code(sf, spec, py_debug, module_definition):
    """ Generate the implementation of a composite module. """

    module = spec.module

    g_declare_limited_api(sf, py_debug)
    g_include_sip_h(sf, module)

    sf.write(
'''

static void sip_import_component_module(PyObject *d, const char *name)
{
    PyObject *mod;

    PyErr_Clear();

    mod = PyImport_ImportModule(name);

    /*
     * Note that we don't complain if the module can't be imported.  This
     * is a favour to Linux distro packagers who like to split PyQt into
     * different sub-packages.
     */
    if (mod)
    {
        PyDict_Merge(d, PyModule_GetDict(mod), 0);
        Py_DECREF(mod);
    }
}
''')

    g_module_docstring(sf, module)
    g_module_init_start(sf, spec)
    sf.write(module_definition)

    sf.write(
'''
    PyObject *sipModule, *sipModuleDict;

    if ((sipModule = PyModule_Create(&sip_module_def)) == SIP_NULLPTR)
        return SIP_NULLPTR;

    sipModuleDict = PyModule_GetDict(sipModule);

''')

    for mod in module.all_imports:
        sf.write(
f'    sip_import_component_module(sipModuleDict, "{mod.fq_py_name}");\n')

    sf.write(
'''
    PyErr_Clear();

    return sipModule;
}
''')


def g_declare_limited_api(sf, py_debug, module=None):
    """ Declare the use of the limited API. """

    if py_debug:
        return

    if module is None or module.use_limited_api:
        sf.write(
'''
#if !defined(Py_LIMITED_API)
#define Py_LIMITED_API
#endif
''')


def g_include_sip_h(sf, module):
    """ Generate the inclusion of sip.h. """

    if module.py_ssize_t_clean:
        sf.write(
'''
#define PY_SSIZE_T_CLEAN
''')

    sf.write(
'''
#include "sip.h"
''')


def g_module_docstring(sf, module):
    """ Generate the definition of the module's optional docstring. """

    if module.docstring is not None:
        sf.write(
f'''
PyDoc_STRVAR(doc_mod_{module.py_name}, "{self.docstring_text(module.docstring)}");
''')


def g_module_init_start(sf, spec):
    """ Generate the start of the Python module initialisation function. """

    if spec.is_composite or spec.c_bindings:
        extern_c = ''
        arg_type = 'void'
    else:
        extern_c = 'extern "C" '
        arg_type = ''

    module_name = spec.module.py_name

    sf.write(
f'''

/* The Python module initialisation function. */
#if defined(SIP_STATIC_MODULE)
{extern_c}PyObject *PyInit_{module_name}({arg_type})
#else
PyMODINIT_FUNC PyInit_{module_name}({arg_type})
#endif
{{
''')
