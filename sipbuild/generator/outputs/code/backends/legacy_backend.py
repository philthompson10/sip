# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from ....specification import ArgumentType, WrappedClass

from ...formatters import fmt_argument_as_cpp_type

from ..utils import cached_name_ref, get_normalised_cached_name, py_scope

from .backend import Backend


class LegacyBackend(Backend):
    """ The backend code generator that handles legacy ABI versions. """

    def g_create_wrapped_module(self, sf, bindings,
        # TODO These will probably be generated here at some point.
        has_sip_strings,
        has_external,
        nr_enum_members,
        has_virtual_error_handlers,
        nr_subclass_convertors,
        is_inst_class,
        is_inst_voidp,
        is_inst_char,
        is_inst_string,
        is_inst_int,
        is_inst_long,
        is_inst_ulong,
        is_inst_longlong,
        is_inst_ulonglong,
        is_inst_double,
        slot_extenders,
        init_extenders
    ):
        """ Generate the code to create a wrapped module. """

        spec = self.spec
        target_abi = spec.target_abi
        module = spec.module
        module_name = module.py_name

        imports_table = self.optional_ptr(len(module.all_imports) != 0,
                'importsTable')
        exported_types = self.optional_ptr(len(module.needed_types) != 0,
                'sipExportedTypes_' + module_name)
        external_types = self.optional_ptr(has_external, 'externalTypesTable')
        typedefs_table = self.optional_ptr(module.nr_typedefs != 0,
                'typedefsTable')

        sf.write(
f'''/* This defines this module. */
sipExportedModuleDef sipModuleAPI_{module_name} = {{
    SIP_NULLPTR,
    {target_abi[1]},
    sipNameNr_{get_normalised_cached_name(module.fq_py_name)},
    0,
    sipStrings_{module_name},
    {imports_table},
''')

        if target_abi < (13, 0):
            qt_api = self.optional_ptr(self.module_supports_qt(), '&qtAPI')
            sf.write(f'    {qt_api},\n')

        sf.write(
f'''    {len(module.needed_types)},
    {exported_types},
    {external_types},
''')

        if self.custom_enums_supported():
            enum_members = self.optional_ptr(nr_enum_members > 0,
                    'enummembers')
            sf.write(
f'''    {nr_enum_members},
    {enum_members},
''')

        veh_table = self.optional_ptr(has_virtual_error_handlers,
                'virtErrorHandlersTable')
        convertors = self.optional_ptr(nr_subclass_convertors > 0,
                'convertorsTable')
        type_instances = self.optional_ptr(is_inst_class, 'typeInstances')
        void_ptr_instances = self.optional_ptr(is_inst_voidp,
                'voidPtrInstances')
        char_instances = self.optional_ptr(is_inst_char, 'charInstances')
        string_instances = self.optional_ptr(is_inst_string, 'stringInstances')
        int_instances = self.optional_ptr(is_inst_int, 'intInstances')
        long_instances = self.optional_ptr(is_inst_long, 'longInstances')
        unsigned_long_instances = self.optional_ptr(is_inst_ulong,
                'unsignedLongInstances')
        long_long_instances = self.optional_ptr(is_inst_longlong,
                'longLongInstances')
        unsigned_long_long_instances = self.optional_ptr(is_inst_ulonglong,
                'unsignedLongLongInstances')
        double_instances = self.optional_ptr(is_inst_double, 'doubleInstances')
        module_license = self.optional_ptr(module.license is not None,
                '&module_license')
        exported_exceptions = self.optional_ptr(module.nr_exceptions > 0,
                'sipExportedExceptions_' + module_name)
        slot_extender_table = self.optional_ptr(slot_extenders,
                'slotExtenders')
        init_extender_table = self.optional_ptr(init_extenders,
                'initExtenders')
        delayed_dtors = self.optional_ptr(module.has_delayed_dtors,
                'sipDelayedDtors')

        sf.write(
f'''    {module.nr_typedefs},
    {typedefs_table},
    {veh_table},
    {convertors},
    {{{type_instances}, {void_ptr_instances}, {char_instances}, {string_instances}, {int_instances}, {long_instances}, {unsigned_long_instances}, {long_long_instances}, {unsigned_long_long_instances}, {double_instances}}},
    {module_license},
    {exported_exceptions},
    {slot_extender_table},
    {init_extender_table},
    {delayed_dtors},
    SIP_NULLPTR,
''')

        if target_abi < (13, 0):
            # The unused version support.
            sf.write(
'''    SIP_NULLPTR,
    SIP_NULLPTR,
''')

        exception_handler = self.optional_ptr(
                (self.abi_has_next_exception_handler() and bindings.exceptions and module.nr_exceptions > 0),
                'sipExceptionHandler_' + module_name)

        sf.write(
f'''    {exception_handler},
}};
''')

        self.g_module_docstring(sf)

        # Generate the storage for the external API pointers.
        sf.write(
f'''

/* The SIP API and the APIs of any imported modules. */
const sipAPIDef *sipAPI_{module_name};
''')

        self.g_pyqt_helper_defns(sf)
        self.g_module_init_start(sf)
        has_module_functions = self.g_module_functions_table(sf, bindings)
        self.g_module_definition(sf, has_module_functions=has_module_functions)
        self._g_module_init_body(sf)

    def g_module_definition(self, sf, has_module_functions=False):
        """ Generate the module definition structure. """

        module = self.spec.module

        if module.docstring is None:
            docstring_ref = 'SIP_NULLPTR'
        else:
            docstring_ref = f'doc_mod_{module.py_name}'

        method_table = self.optional_ptr(has_module_functions, 'sip_methods')

        sf.write(
f'''    static PyModuleDef sip_module_def = {{
        PyModuleDef_HEAD_INIT,
        "{module.fq_py_name}",
        {docstring_ref},
        -1,
        {method_table},
        SIP_NULLPTR,
        SIP_NULLPTR,
        SIP_NULLPTR,
        SIP_NULLPTR
    }};
''')

    def abi_has_deprecated_message(self):
        """ Return True if the ABI implements sipDeprecated() with a message.
        """

        return self._abi_version_check((12, 16), (13, 9))

    def abi_has_next_exception_handler(self):
        """ Return True if the ABI implements sipNextExceptionHandler(). """

        return self._abi_version_check((12, 9), (13, 1))

    def abi_has_working_char_conversion(self):
        """ Return True if the ABI has working char to/from a Python integer
        converters (ie. char is not assumed to be signed).
        """

        return self._abi_version_check((12, 15), (13, 8))

    def abi_supports_array(self):
        """ Return True if the ABI supports sip.array. """

        return self._abi_version_check((12, 11), (13, 4))

    def custom_enums_supported(self):
        """ Return True if custom enums are supported. """

        return self.spec.target_abi[0] < 13

    def module_supports_qt(self):
        """ Return True if the module implements Qt support. """

        spec = self.spec

        return spec.pyqt_qobject is not None and spec.pyqt_qobject.iface_file.module is spec.module

    def _abi_version_check(self, min_12, min_13):
        """ Return True if the ABI version meets minimum version requirements.
        """

        target_abi = self.spec.target_abi

        return target_abi >= min_13 or (min_12 <= target_abi < (13, 0))

    def _g_module_init_body(self, sf):
        """ Generate the body of the module initialisation function. """

        # TODO Some of this is common to the current ABI and needs moving to
        # the superclass.
        spec = self.spec
        module = spec.module
        module_name = module.py_name

        sf.write('\n    PyObject *sipModule, *sipModuleDict;\n')

        if spec.sip_module:
            sf.write('    PyObject *sip_sipmod, *sip_capiobj;\n\n')

        # Generate any pre-initialisation code.
        sf.write_code(module.preinitialisation_code)

        sf.write(
'''    /* Initialise the module and get it's dictionary. */
    if ((sipModule = PyModule_Create(&sip_module_def)) == SIP_NULLPTR)
        return SIP_NULLPTR;

    sipModuleDict = PyModule_GetDict(sipModule);

''')

        self._g_sip_api(sf)

        # Generate any initialisation code.
        sf.write_code(module.initialisation_code)

        abi_major, abi_minor = spec.target_abi

        sf.write(
f'''    /* Export the module and publish it's API. */
    if (sipExportModule(&sipModuleAPI_{module_name}, {abi_major}, {abi_minor}, 0) < 0)
    {{
        Py_DECREF(sipModule);
        return SIP_NULLPTR;
    }}
''')

        self.g_pyqt_helper_init(sf)

        sf.write(
f'''
    /* Initialise the module now all its dependencies have been set up. */
    if (sipInitModule(&sipModuleAPI_{module_name}, sipModuleDict) < 0)
    {{
        Py_DECREF(sipModule);
        return SIP_NULLPTR;
    }}
''')

        self._g_types_inline(sf)
        self._g_py_objects(sf)

        # Create any exception objects.
        for exception in spec.exceptions:
            if exception.iface_file.module is not module:
                continue

            if exception.exception_nr < 0:
                continue

            if exception.builtin_base_exception is not None:
                exception_type = 'PyExc_' + exception.builtin_base_exception
            else:
                exception_type = 'sipException_' + exception.defined_base_exception.iface_file.fq_cpp_name.as_word

            sf.write(
f'''
    if ((sipExportedExceptions_{module_name}[{exception.exception_nr}] = PyErr_NewException(
            "{module_name}.{exception.py_name}",
            {exception_type}, SIP_NULLPTR)) == SIP_NULLPTR || PyDict_SetItemString(sipModuleDict, "{exception.py_name}", sipExportedExceptions_{module_name}[{exception.exception_nr}]) < 0)
    {{
        Py_DECREF(sipModule);
        return SIP_NULLPTR;
    }}
''')

        if module.nr_exceptions > 0:
            sf.write(
f'''
    sipExportedExceptions_{module_name}[{module.nr_exceptions}] = SIP_NULLPTR;
''')

        # Generate the enum and QFlag meta-type registrations for PyQt6.
        if self.pyqt6_supported():
            for enum in spec.enums:
                if enum.module is not module or enum.fq_cpp_name is None:
                    continue

                if enum.is_protected:
                    continue

                if isinstance(enum.scope, WrappedClass) and enum.scope.pyqt_no_qmetaobject:
                    continue

                sf.write(f'    qMetaTypeId<{enum.fq_cpp_name.as_cpp}>();\n')

            for mapped_type in spec.mapped_types:
                if mapped_type.iface_file.module is not module:
                    continue

                if mapped_type.pyqt_flags == 0:
                    continue

                mapped_type_type = fmt_argument_as_cpp_type(spec,
                        mapped_type.type, plain=True, no_derefs=True)
                sf.write(f'    qMetaTypeId<{mapped_type_type}>();\n')

        # Generate any post-initialisation code. */
        sf.write_code(module.postinitialisation_code)

        sf.write(
'''
    return sipModule;
}
''')

    # The types that are implemented as PyObject*.
    _PY_OBJECT_TYPES = (ArgumentType.PYOBJECT, ArgumentType.PYTUPLE,
        ArgumentType.PYLIST, ArgumentType.PYDICT, ArgumentType.PYCALLABLE,
        ArgumentType.PYSLICE, ArgumentType.PYTYPE, ArgumentType.PYBUFFER,
        ArgumentType.PYENUM)

    def _g_py_objects(self, sf):
        """ Generate the inline code to add a set of Python objects to a module
        dictionary.
        """

        spec = self.spec

        # Note that we should add these via a table (like int, etc) but that
        # will require a major API version change so this will do for now.
        # TODO Do it for ABI v14..

        no_intro = True

        for variable in spec.variables:
            if variable.module is not spec.module:
                continue

            if variable.type.type not in self._PY_OBJECT_TYPES:
                continue

            if variable.needs_handler:
                continue

            if no_intro:
                sf.write('\n    /* Define the Python objects wrapped as such. */\n')
                no_intro = False

            py_name = cached_name_ref(variable.py_name)
            cpp_name = self.scoped_variable_name(variable)

            sf.write(f'    PyDict_SetItemString(sipModuleDict, {py_name}, {cpp_name});\n')

    def _g_sip_api(self, sf):
        """ Generate the code to get the sip API. """

        spec = self.spec
        sip_module_name = spec.sip_module
        module_name = spec.module.py_name

        if sip_module_name:
            # Note that we don't use PyCapsule_Import() because we thought
            # (incorrectly) that it doesn't handle package.module.attribute.

            sf.write(
f'''    /* Get the SIP module's API. */
    if ((sip_sipmod = PyImport_ImportModule("{sip_module_name}")) == SIP_NULLPTR)
    {{
        Py_DECREF(sipModule);
        return SIP_NULLPTR;
    }}

    sip_capiobj = PyDict_GetItemString(PyModule_GetDict(sip_sipmod), "_C_API");
    Py_DECREF(sip_sipmod);

    if (sip_capiobj == SIP_NULLPTR || !PyCapsule_CheckExact(sip_capiobj))
    {{
        PyErr_SetString(PyExc_AttributeError, "{sip_module_name}._C_API is missing or has the wrong type");
        Py_DECREF(sipModule);
        return SIP_NULLPTR;
    }}

''')

            if spec.c_bindings:
                c_api = f'(const sipAPIDef *)PyCapsule_GetPointer(sip_capiobj, "{sip_module_name}._C_API")'
            else:
                c_api = f'reinterpret_cast<const sipAPIDef *>(PyCapsule_GetPointer(sip_capiobj, "{sip_module_name}._C_API"))'

            sf.write(
f'''    sipAPI_{module_name} = {c_api};

    if (sipAPI_{module_name} == SIP_NULLPTR)
    {{
        Py_DECREF(sipModule);
        return SIP_NULLPTR;
    }}

''')
        else:
            # If there is no sip module name then we are getting the API from a
            # non-shared sip module.
            sf.write(
f'''    if ((sipAPI_{module_name} = sip_init_library(sipModuleDict)) == SIP_NULLPTR)
        return SIP_NULLPTR;

''')

    def _g_types_inline(self, sf):
        """ Generate the inline code to add a set of generated type instances
        to a dictionary.
        """

        spec = self.spec
        no_intro = True

        for variable in spec.variables:
            if variable.module is not spec.module:
                continue

            if variable.type.type not in (ArgumentType.CLASS, ArgumentType.MAPPED, ArgumentType.ENUM):
                continue

            if variable.needs_handler:
                continue

            # Skip classes that don't need inline code.
            if spec.c_bindings or variable.access_code is not None or len(variable.type.derefs) != 0:
                continue

            if no_intro:
                sf.write(
'''
    /*
     * Define the class, mapped type and enum instances that have to be
     * added inline.
     */
''')

                no_intro = False

            if py_scope(variable.scope) is None:
                dict_name = 'sipModuleDict'
            else:
                dict_name = f'(PyObject *)sipTypeAsPyTypeObject({self.gto_name(variable.scope)})'

            py_name = cached_name_ref(variable.py_name)
            ptr = '&' + self.scoped_variable_name(variable)

            if variable.type.is_const:
                type_name = fmt_argument_as_cpp_type(spec, variable.type,
                        plain=True, no_derefs=True)
                ptr = f'const_cast<{type_name} *>({ptr})'

            sf.write(f'    sipAddTypeInstance({dict_name}, {py_name}, {ptr}, {self.gto_name(variable.type.definition)});\n')
