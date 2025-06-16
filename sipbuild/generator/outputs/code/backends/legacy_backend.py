# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from .backend import Backend


class LegacyBackend(Backend):
    """ The backend code generator that handles legacy ABI versions. """

    def g_create_wrapped_module(self, sf, bindings,
        # TODO These will probably be generated here at some point.
        has_name_cache,
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

        imports_table = _optional_ptr(len(module.all_imports) != 0,
                'importsTable')
        exported_types = _optional_ptr(len(module.needed_types) != 0,
                'sipExportedTypes_' + module_name)
        external_types = _optional_ptr(has_external, 'externalTypesTable')
        typedefs_table = _optional_ptr(module.nr_typedefs != 0,
                'typedefsTable')

        sf.write(
f'''/* This defines this module. */
sipExportedModuleDef sipModuleAPI_{module_name} = {{
    SIP_NULLPTR,
    {target_abi[1]},
    sipNameNr_{self.get_normalised_cached_name(module.fq_py_name)},
    0,
    sipStrings_{module_name},
    {imports_table},
''')

        if target_abi < (13, 0):
            qt_api = _optional_ptr(self.module_supports_qt(), '&qtAPI')
            sf.write(f'    {qt_api},\n')

        sf.write(
f'''    {len(module.needed_types)},
    {exported_types},
    {external_types},
''')

        if self.custom_enums_supported():
            enum_members = _optional_ptr(nr_enum_members > 0, 'enummembers')
            sf.write(
f'''    {nr_enum_members},
    {enum_members},
''')

        veh_table = _optional_ptr(has_virtual_error_handlers,
                'virtErrorHandlersTable')
        convertors = _optional_ptr(nr_subclass_convertors > 0,
                'convertorsTable')
        type_instances = _optional_ptr(is_inst_class, 'typeInstances')
        void_ptr_instances = _optional_ptr(is_inst_voidp, 'voidPtrInstances')
        char_instances = _optional_ptr(is_inst_char, 'charInstances')
        string_instances = _optional_ptr(is_inst_string, 'stringInstances')
        int_instances = _optional_ptr(is_inst_int, 'intInstances')
        long_instances = _optional_ptr(is_inst_long, 'longInstances')
        unsigned_long_instances = _optional_ptr(is_inst_ulong,
                'unsignedLongInstances')
        long_long_instances = _optional_ptr(is_inst_longlong,
                'longLongInstances')
        unsigned_long_long_instances = _optional_ptr(is_inst_ulonglong,
                'unsignedLongLongInstances')
        double_instances = _optional_ptr(is_inst_double, 'doubleInstances')
        module_license = _optional_ptr(module.license is not None,
                '&module_license')
        exported_exceptions = _optional_ptr(module.nr_exceptions > 0,
                'sipExportedExceptions_' + module_name)
        slot_extender_table = _optional_ptr(slot_extenders, 'slotExtenders')
        init_extender_table = _optional_ptr(init_extenders, 'initExtenders')
        delayed_dtors = _optional_ptr(module.has_delayed_dtors,
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

        exception_handler = _optional_ptr(
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


def _optional_ptr(is_ptr, name):
    """ Return an appropriate reference to an optional pointer. """

    return name if is_ptr else 'SIP_NULLPTR'
