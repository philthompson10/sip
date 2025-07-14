# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from .....sip_module_configuration import SipModuleConfiguration

from ....scoped_name import STRIP_GLOBAL
from ....specification import ArgumentType, WrappedEnum

from ...formatters import fmt_class_as_scoped_name

from ..utils import get_normalised_cached_name, has_member_docstring, py_scope


class Backend:
    """ The backend code generator for the latest ABI. """

    def __init__(self, spec):
        """ Initialise the backend. """

        self.spec = spec

    @classmethod
    def factory(cls, spec):
        """ Return an appropriate backend for the target ABI. """

        if spec.target_abi >= (14, 0):
            return cls(spec)

        from .legacy_backend import LegacyBackend

        return LegacyBackend(spec)

    def g_create_wrapped_module(self, sf, bindings,
        # TODO These will probably be generated here at some point.
        has_sip_strings,
        has_external,
        nr_enum_members,
        has_virtual_error_handlers,
        nr_subclass_convertors,
        static_values_state,
        slot_extenders,
        init_extenders
    ):
        """ Generate the code to generate a wrapped module. """

        spec = self.spec
        target_abi = spec.target_abi
        module = spec.module
        module_name = module.py_name

        sf.write(
f'''/* The wrapped module's immutable definition. */
static const sipWrappedModuleDef sipWrappedModule_{module_name} = {{
    .abi_major = {target_abi[0]},
    .abi_minor = {target_abi[1]},
    .sip_configuration = 0x{spec.sip_module_configuration:04x},
''')

        if len(module.all_imports) != 0:
            sf.write('    .imports = importsTable,\n')

        if len(module.needed_types) != 0:
            sf.write(f'    .nr_types = {len(module.needed_types)},\n')
            sf.write(f'    .types = sipExportedTypes_{module_name},\n')

        if has_external:
            sf.write('    .imports = externalTypesTable,\n')

        if self.custom_enums_supported() and nr_enum_members != 0:
            sf.write(f'    .nr_enum_members = {nr_enum_members},\n')
            sf.write('    .enum_members = enum_members,\n')

        if module.nr_typedefs != 0:
            sf.write(f'    .nr_typedefs = {module.nr_typedefs},\n')
            sf.write('    .typedefs = typedefsTable,\n')

        if has_virtual_error_handlers:
            sf.write('    .virterrorhandlers = virtErrorHandlersTable,\n')

        if nr_subclass_convertors != 0:
            sf.write('    .convertors = convertorsTable,\n')

        if static_values_state != 0:
            sf.write(f'    .nr_static_values = {static_values_state},\n')
            sf.write('    .static_values = sipStaticValuesTable,\n')

        if module.license is not None:
            sf.write('    .license = &module_license,\n')

        if slot_extenders:
            sf.write('    .slotextend = slotExtenders,\n')

        if init_extenders:
            sf.write('    .initextend = initExtenders,\n')

        if module.has_delayed_dtors:
            sf.write('    .delayeddtors = sipDelayedDtors,\n')

        if bindings.exceptions and module.nr_exceptions != 0:
            sf.write(f'    .exception_handler = sipExceptionHandler_{module_name},\n')

        sf.write('};\n')

        self.g_module_docstring(sf)
        self.g_pyqt_helper_defns(sf)
        self._g_module_clear(sf)
        self._g_module_exec(sf)
        self._g_module_free(sf)
        self._g_module_traverse(sf)
        self.g_module_init_start(sf)
        has_module_functions = self.g_module_functions_table(sf, bindings)
        self.g_module_definition(sf, has_module_functions=has_module_functions)

        sf.write(
'''
    return PyModuleDef_Init(&sip_wrapped_module_def);
}
''')

    def g_module_definition(self, sf, has_module_functions=False):
        """ Generate the module definition structure. """

        module = self.spec.module

        # TODO This value should be taken from a new option of the %Module
        # directive and default to Py_MOD_MULTIPLE_INTERPRETERS_NOT_SUPPORTED.
        interp_support = 'Py_MOD_MULTIPLE_INTERPRETERS_NOT_SUPPORTED'

        sf.write(
f'''    static PyModuleDef_Slot sip_wrapped_module_slots[] = {{
        {{Py_mod_exec, (void *)wrapped_module_exec}},
#if PY_VERSION_HEX >= 0x030c0000
        {{Py_mod_multiple_interpreters, {interp_support}}},
#endif
#if PY_VERSION_HEX >= 0x030d0000
        {{Py_mod_gil, Py_MOD_GIL_USED}},
#endif
        {{0, SIP_NULLPTR}}
    }};

    static PyModuleDef sip_wrapped_module_def = {{
        .m_base = PyModuleDef_HEAD_INIT,
        .m_name = "{module.fq_py_name}",
        .m_size = sizeof (sipWrappedModuleState),
        .m_slots = sip_wrapped_module_slots,
        .m_clear = wrapped_module_clear,
        .m_traverse = wrapped_module_traverse,
        .m_free = wrapped_module_free,
''')

        if module.docstring is not None:
            # TODO The name should have a sip_ prefix.
            sf.write(f'        .m_doc = doc_mod_{module.py_name},\n')

        if has_module_functions:
            sf.write('        .m_methods = sip_methods,\n')

        sf.write('    };\n')

    def g_module_docstring(self, sf):
        """ Generate the definition of the module's optional docstring. """

        module = self.spec.module

        if module.docstring is not None:
            sf.write(
f'''
PyDoc_STRVAR(doc_mod_{module.py_name}, "{self.docstring_text(module.docstring)}");
''')

    def g_module_functions_table(self, sf, bindings):
        """ Generate the table of module functions and return True if anything
        was actually generated.
        """

        spec = self.spec
        module = spec.module

        has_module_functions = self._g_module_function_table_entries(sf,
                bindings, module.global_functions)

        # Generate the module functions for any hidden namespaces.
        for klass in spec.classes:
            if klass.iface_file.module is module and klass.is_hidden_namespace:
                has_module_functions = self._g_module_function_table_entries(
                        sf, bindings, klass.members,
                        has_module_functions=has_module_functions)

        if has_module_functions:
            sf.write(
'''        {SIP_NULLPTR, SIP_NULLPTR, 0, SIP_NULLPTR}
    };
''')

        return has_module_functions

    def g_module_init_start(self, sf):
        """ Generate the start of the Python module initialisation function.
        """

        spec = self.spec

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

    def g_pyqt_helper_defns(self, sf):
        """ Generate the PyQt helper definitions. """

        # TODO Needs changing for ABI v14.

        if self.pyqt5_supported() or self.pyqt6_supported():
            module_name = self.spec.module.py_name

            sf.write(
f'''
sip_qt_metaobject_func sip_{module_name}_qt_metaobject;
sip_qt_metacall_func sip_{module_name}_qt_metacall;
sip_qt_metacast_func sip_{module_name}_qt_metacast;
''')

    def g_pyqt_helper_init(self, sf):
        """ Initialise the PyQt helpers. """

        # TODO Needs changing for ABI v14.

        if self.pyqt5_supported() or self.pyqt6_supported():
            module_name = self.spec.module.py_name

            sf.write(
f'''

    sip_{module_name}_qt_metaobject = (sip_qt_metaobject_func)sipImportSymbol("qtcore_qt_metaobject");
    sip_{module_name}_qt_metacall = (sip_qt_metacall_func)sipImportSymbol("qtcore_qt_metacall");
    sip_{module_name}_qt_metacast = (sip_qt_metacast_func)sipImportSymbol("qtcore_qt_metacast");

    if (!sip_{module_name}_qt_metacast)
        Py_FatalError("Unable to import qtcore_qt_metacast");
''')

    def g_static_values_table(self, sf, scope=None):
        """ Generate the table of static values for a scope and return the
        length of the table.
        """

        nr_static_values = 0

        # Get the sorted list of variables.
        variables = list(self.variables_in_scope(scope))
        variables.sort(key=lambda k: k.py_name)

        # TODO - all we are doing is working out the sipTypeID?
        for variable in variables:
            v_type = variable.type

            # TODO - the following is based on legacy code but the types must
            # be more precise as we will be dereferencing casted pointer rather
            # than letting the compiler handle it.
            if v_type.type in (ArgumentType.ASCII_STRING, ArgumentType.LATIN1_STRING, ArgumentType.UTF8_STRING, ArgumentType.SSTRING, ArgumentType.USTRING, ArgumentType.STRING, ArgumentType.WSTRING):
                if len(v_type.derefs) == 0:
                    # TODO char/wchar_t
                    pass
                else:
                    # TODO char */wchar_t *
                    pass

            elif v_type.type is ArgumentType.CLASS or (v_type.type is ArgumentType.ENUM and v_type.definition.fq_cpp_name is not None):
                # TODO class/named enum
                pass

            elif v_type.type in (ArgumentType.INT, ArgumentType.CINT):
                type_id = 'sipTypeID_int'

            elif v_type.type in (ArgumentType.FLOAT, ArgumentType.CFLOAT, ArgumentType.DOUBLE, ArgumentType.CDOUBLE):
                # TODO double
                pass

            else:
                continue

            if nr_static_values == 0:
                if scope is None:
                    scope_type = 'module'
                    suffix = ''
                else:
                    scope_type = 'type'
                    suffix = '_' + scope.iface_file.fq_cpp_name.as_word

                sf.write(
f'''
/* Define the static values for the {scope_type}. */
static sipStaticValuesDef sipStaticValuesTable{suffix}[] = {{
''')

            name = variable.py_name
            flags = 'SIP_SV_RO' if v_type.is_const else '0'
            value = variable.fq_cpp_name.cpp_stripped(STRIP_GLOBAL)

            sf.write(f'    {{"{name}", {type_id}, {flags}, (void *)&{value}}}\n')

            nr_static_values += 1

        if nr_static_values != 0:
            sf.write('};\n')

        return nr_static_values

    def abi_has_deprecated_message(self):
        """ Return True if the ABI implements sipDeprecated() with a message.
        """

        return True

    def abi_has_next_exception_handler(self):
        """ Return True if the ABI implements sipNextExceptionHandler(). """

        return True

    def abi_has_working_char_conversion(self):
        """ Return True if the ABI has working char to/from a Python integer
        converters (ie. char is not assumed to be signed).
        """

        return True

    def abi_supports_array(self):
        """ Return True if the ABI supports sip.array. """

        return True

    def custom_enums_supported(self):
        """ Return True if custom enums are supported. """

        return SipModuleConfiguration.CustomEnums in self.spec.sip_module_configuration

    @staticmethod
    def docstring_text(docstring):
        """ Return the text of a docstring. """

        text = docstring.text

        # Remove any single trailing newline.
        if text.endswith('\n'):
            text = text[:-1]

        s = ''

        for ch in text:
            if ch == '\n':
                # Let the compiler concatanate lines.
                s += '\\n"\n"'
            elif ch in r'\"':
                s += '\\'
                s += ch
            elif ch.isprintable():
                s += ch
            else:
                s += f'\\{ord(ch):03o}'

        return s

    @staticmethod
    def gto_name(wrapped_object):
        """ Return the name of the generated type object for a wrapped object.
        """

        fq_cpp_name = wrapped_object.fq_cpp_name if isinstance(wrapped_object, WrappedEnum) else wrapped_object.iface_file.fq_cpp_name

        return 'sipType_' + fq_cpp_name.as_word

    @staticmethod
    def optional_ptr(is_ptr, name):
        """ Return an appropriate reference to an optional pointer. """

        return name if is_ptr else 'SIP_NULLPTR'

    def pyqt5_supported(self):
        """ Return True if the PyQt5 plugin was specified. """

        return 'PyQt5' in self.spec.plugins

    def pyqt6_supported(self):
        """ Return True if the PyQt6 plugin was specified. """

        return 'PyQt6' in self.spec.plugins

    def scoped_class_name(self, klass):
        """ Return a scoped class name as a string.  Protected classes have to
        be explicitly scoped.
        """

        return fmt_class_as_scoped_name(self.spec, klass,
                scope=klass.iface_file)

    def scoped_variable_name(self, variable):
        """ Return a scoped variable name as a string.  This should be used
        whenever the scope may be the instantiation of a template which
        specified /NoTypeName/.
        """

        scope = variable.scope
        fq_cpp_name = variable.fq_cpp_name

        if scope is None:
            return fq_cpp_name.as_cpp

        return self.scoped_class_name(scope) + '::' + fq_cpp_name.base_name

    def variables_in_scope(self, scope, check_handler=True):
        """ An iterator over the variables in a scope. """

        spec = self.spec

        for variable in spec.variables:
            if py_scope(variable.scope) is scope and variable.module is spec.module:
                if check_handler and variable.needs_handler:
                    continue

                yield variable

    def _g_module_clear(self, sf):
        """ Generate the module clear slot. """

        sf.write(
'''

/* The wrapped module's clear slot. */
static int wrapped_module_clear(PyObject *wmod)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    return wms->sip_api->api_wrapped_module_clear(wms);
}
''')

    def _g_module_exec(self, sf):
        """ Generate the module exec slot. """

        spec = self.spec
        sip_module_name = spec.sip_module
        module = spec.module
        module_name = module.py_name

        sf.write(
'''

/* The wrapped module's exec function. */
static int wrapped_module_exec(PyObject *sipModule)
{
''')

        sf.write_code(module.preinitialisation_code)

        if sip_module_name:
            sip_init_func_ref = 'sip_init_func'
            sip_module_ref = 'sip_sip_module'
            sf.write(
f'''    PyObject *{sip_module_ref} = PyImport_ImportModule("{sip_module_name}");
    if ({sip_module_ref} == SIP_NULLPTR)
        return -1;

    PyObject *sip_capsule = PyDict_GetItemString(PyModule_GetDict(sip_sip_module), "_C_BOOTSTRAP");
    if (!PyCapsule_IsValid(sip_capsule, "_C_BOOTSTRAP"))
    {{
        Py_XDECREF(sip_capsule);
        Py_DECREF(sip_sip_module);
        return -1;
    }}

    sipBootstrapFunc sip_bootstrap = (sipBootstrapFunc)PyCapsule_GetPointer(sip_capsule, "_C_BOOTSTRAP");
    Py_DECREF(sip_capsule);

    sipWrappedModuleInitFunc {sip_init_func_ref} = sip_bootstrap({spec.target_abi[0]});
    if ({sip_init_func_ref} == SIP_NULLPTR)
    {{
        Py_DECREF({sip_module_ref});
        return -1;
    }}

''')
        else:
            sip_init_func_ref = 'sip_api_wrapped_module_init';
            sip_module_ref = 'SIP_NULLPTR';

        sf.write_code(module.initialisation_code)

        self.g_pyqt_helper_init(sf)

        sf.write(
f'''    if ({sip_init_func_ref}(sipModule, &sipWrappedModule_{module_name}, {sip_module_ref}) < 0)
        return -1;
''')

        # TODO Handle post-initialisation code.  Get the module dict if the
        # code uses it (sipModuleDict).

        sf.write(
'''
    return 0;
}
''')

    def _g_module_free(self, sf):
        """ Generate the module free slot. """

        sf.write(
'''

/* The wrapped module's free slot. */
static void wrapped_module_free(void *wmod_ptr)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            (PyObject *)wmod_ptr);

    wms->sip_api->api_wrapped_module_free(wms);
}
''')

    def _g_module_function_table_entries(self, sf, bindings, members,
            has_module_functions=False):
        """ Generate the entries in a table of PyMethodDef for module
        functions.
        """

        for member in members:
            if member.py_slot is None:
                if not has_module_functions:
                    sf.write('    static PyMethodDef sip_methods[] = {\n')
                    has_module_functions = True

                py_name = get_normalised_cached_name(member.py_name)
                sf.write(f'        {{sipName_{py_name}, ')

                if member.no_arg_parser or member.allow_keyword_args:
                    sf.write(f'SIP_MLMETH_CAST(func_{member.py_name.name}), METH_VARARGS|METH_KEYWORDS')
                else:
                    sf.write(f'func_{member.py_name.name}, METH_VARARGS')

                docstring = self.optional_ptr(
                        has_member_docstring(bindings, member,
                                self.spec.module.overloads),
                        'doc_' + member.py_name.name)
                sf.write(f', {docstring}}},\n')

        return has_module_functions

    def _g_module_traverse(self, sf):
        """ Generate the module traverse slot. """

        sf.write(
'''

/* The wrapped module's traverse slot. */
static int wrapped_module_traverse(PyObject *wmod, visitproc visit, void *arg)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            wmod);

    return wms->sip_api->api_wrapped_module_traverse(wms, visit, arg);
}
''')
