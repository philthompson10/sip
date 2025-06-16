# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from .....sip_module_configuration import SipModuleConfiguration


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
        """ Generate the code to generate a wrapped module. """

        spec = self.spec
        target_abi = spec.target_abi
        module = spec.module
        module_name = module.py_name

        sf.write(
f'''/* This is the immutable definition of the wrapped module. */
static sipWrappedModuleDef sipWrappedModule_{module_name} = {{
    .wm_abi_major = {target_abi[0]},
    .wm_abi_minor = {target_abi[1]},
    .wm_sip_configuration = 0x{spec.sip_module_configuration:04x},
''')

        if has_name_cache:
            sf.write(f'    .wm_strings = sipStrings_{module_name},\n')

        if len(module.all_imports) != 0:
            sf.write('    .wm_imports = importsTable,\n')

        if len(module.needed_types) != 0:
            sf.write(f'    .wm_nrtypes = {len(module.needed_types)},\n')
            sf.write(f'    .wm_types = sipExportedTypes_{module_name},\n')

        if has_external:
            sf.write('    .wm_imports = externalTypesTable,\n')

        if self.custom_enums_supported() and nr_enum_members != 0:
            sf.write(f'    .wm_nrenummembers = {nr_enum_members},\n')
            sf.write('    .wm_enummembers = enummembers,\n')

        if module.nr_typedefs != 0:
            sf.write(f'    .wm_nrtypedefs = {module.nr_typedefs},\n')
            sf.write('    .wm_typedefs = typedefsTable,\n')

        if has_virtual_error_handlers:
            sf.write('    .wm_virterrorhandlers = virtErrorHandlersTable,\n')

        if nr_subclass_convertors != 0:
            sf.write('    .wm_convertors = convertorsTable,\n')

        if is_inst_class or is_inst_voidp or is_inst_char or \
           is_inst_string or is_inst_int or is_inst_long or is_inst_ulong or \
           is_inst_longlong or is_inst_ulonglong or is_inst_double:
            sf.write('    .wm_instances = {\n')

            if is_inst_class:
                sf.write('        .id_type = typeInstances,\n')

            if is_inst_voidp:
                sf.write('        .id_voidp = voidPtrInstances,\n')

            if is_inst_char:
                sf.write('        .id_char = charInstances,\n')

            if is_inst_string:
                sf.write('        .id_string = stringInstances,\n')

            if is_inst_int:
                sf.write('        .id_int = intInstances,\n')

            if is_inst_long:
                sf.write('        .id_long = longInstances,\n')

            if is_inst_ulong:
                sf.write('        .id_ulong = unsignedLongInstances,\n')

            if is_inst_longlong:
                sf.write('        .id_llong = longLongInstances,\n')

            if is_inst_ulonglong:
                sf.write('        .id_ullong = unsignedLongLongInstances,\n')

            if is_inst_double:
                sf.write('        .id_double = doubleInstances,\n')

            sf.write('    },\n')

        if module.license is not None:
            sf.write('    .wm_license = &module_license,\n')

        if slot_extenders:
            sf.write('    .wm_slotextend = slotExtenders,\n')

        if init_extenders:
            sf.write('    .wm_initextend = initExtenders,\n')

        if bindings.exceptions and module.nr_exceptions != 0:
            sf.write(f'    .wm_exception_handler = sipExceptionHandler_{module_name},\n')

        sf.write('};\n')

        self.g_module_docstring(sf)
        self.g_pyqt_helper_defns(sf)
        self.g_module_init_start(sf)

    def g_module_docstring(self, sf):
        """ Generate the definition of the module's optional docstring. """

        module = self.spec.module

        if module.docstring is not None:
            sf.write(
f'''
"PyDoc_STRVAR(doc_mod_{module.py_name}, "{_docstring_text(module.docstring)}");
''')

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

        if self.pyqt5_supported() or self.pyqt6_supported():
            module_name = self.spec.module.py_name

            sf.write(
f'''
sip_qt_metaobject_func sip_{module_name}_qt_metaobject;
sip_qt_metacall_func sip_{module_name}_qt_metacall;
sip_qt_metacast_func sip_{module_name}_qt_metacast;
''')

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
    def get_normalised_cached_name(cached_name):
        """ Return the normalised form of a cached name. """

        # If the name seems to be a template then just use the offset to ensure
        # that it is unique.
        if '<' in cached_name.name:
            return str(cached_name.offset)

        # Handle C++ and Python scopes.
        return cached_name.name.replace(':', '_').replace('.', '_')

    def pyqt5_supported(self):
        """ Return True if the PyQt5 plugin was specified. """

        return 'PyQt5' in self.spec.plugins

    def pyqt6_supported(self):
        """ Return True if the PyQt6 plugin was specified. """

        return 'PyQt6' in self.spec.plugins
