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
        static_variables_state,
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

        if static_variables_state != 0:
            sf.write(f'    .nr_static_variables = {static_variables_state},\n')
            sf.write('    .static_variables = sipStaticVariablesTable,\n')

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

    def g_function_init(self, sf):
        """ Generate the code at the start of function implementation. """

        sf.write('    const sipAPIDef *sipAPI = sipGetAPI(sipModule);\n')

    def g_method_init(self, sf):
        """ Generate the code at the start of method implementation. """

        # TODO
        pass

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

    def g_sip_api(self, sf, module_name):
        """ Generate the SIP API as seen by generated code. """

        # TODO These have been reviewed as part of the public v14 API.

        # TODO These have been reviewed as part of the private v14 API.
        sf.write(
f'''
#define sipNoFunction               sipAPI->api_no_function
#define sipNoMethod                 sipAPI->api_no_method
#define sipParseArgs                sipAPI->api_parse_args
#define sipParseKwdArgs             sipAPI->api_parse_kwd_args
#define sipParsePair                sipAPI->api_parse_pair
''')

        # TODO These have yet to be reviewed.
        sf.write(
f'''#define sipMalloc                   sipAPI->api_malloc
#define sipFree                     sipAPI->api_free
#define sipBuildResult              sipAPI->api_build_result
#define sipCallMethod               sipAPI->api_call_method
#define sipCallProcedureMethod      sipAPI->api_call_procedure_method
#define sipCallErrorHandler         sipAPI->api_call_error_handler
#define sipParseResultEx            sipAPI->api_parse_result_ex
#define sipParseResult              sipAPI->api_parse_result
#define sipInstanceDestroyed        sipAPI->api_instance_destroyed
#define sipInstanceDestroyedEx      sipAPI->api_instance_destroyed_ex
#define sipConvertFromSequenceIndex sipAPI->api_convert_from_sequence_index
#define sipConvertFromSliceObject   sipAPI->api_convert_from_slice_object
#define sipConvertFromVoidPtr       sipAPI->api_convert_from_void_ptr
#define sipConvertToVoidPtr         sipAPI->api_convert_to_void_ptr
#define sipAddException             sipAPI->api_add_exception
#define sipAbstractMethod           sipAPI->api_abstract_method
#define sipBadClass                 sipAPI->api_bad_class
#define sipBadCatcherResult         sipAPI->api_bad_catcher_result
#define sipBadCallableArg           sipAPI->api_bad_callable_arg
#define sipBadOperatorArg           sipAPI->api_bad_operator_arg
#define sipTrace                    sipAPI->api_trace
#define sipTransferBack             sipAPI->api_transfer_back
#define sipTransferTo               sipAPI->api_transfer_to
#define sipSimpleWrapper_Type       sipAPI->api_simplewrapper_type
#define sipWrapper_Type             sipAPI->api_wrapper_type
#define sipWrapperType_Type         sipAPI->api_wrappertype_type
#define sipVoidPtr_Type             sipAPI->api_voidptr_type
#define sipGetPyObject              sipAPI->api_get_pyobject
#define sipGetAddress               sipAPI->api_get_address
#define sipGetMixinAddress          sipAPI->api_get_mixin_address
#define sipGetCppPtr                sipAPI->api_get_cpp_ptr
#define sipGetComplexCppPtr         sipAPI->api_get_complex_cpp_ptr
#define sipCallHook                 sipAPI->api_call_hook
#define sipEndThread                sipAPI->api_end_thread
#define sipRaiseUnknownException    sipAPI->api_raise_unknown_exception
#define sipRaiseTypeException       sipAPI->api_raise_type_exception
#define sipBadLengthForSlice        sipAPI->api_bad_length_for_slice
#define sipAddTypeInstance          sipAPI->api_add_type_instance
#define sipPySlotExtend             sipAPI->api_pyslot_extend
#define sipAddDelayedDtor           sipAPI->api_add_delayed_dtor
#define sipCanConvertToType         sipAPI->api_can_convert_to_type
#define sipConvertToType            sipAPI->api_convert_to_type
#define sipForceConvertToType       sipAPI->api_force_convert_to_type
#define sipConvertToEnum            sipAPI->api_convert_to_enum
#define sipConvertToBool            sipAPI->api_convert_to_bool
#define sipReleaseType              sipAPI->api_release_type
#define sipConvertFromType          sipAPI->api_convert_from_type
#define sipConvertFromNewType       sipAPI->api_convert_from_new_type
#define sipConvertFromNewPyType     sipAPI->api_convert_from_new_pytype
#define sipConvertFromEnum          sipAPI->api_convert_from_enum
#define sipGetState                 sipAPI->api_get_state
#define sipExportSymbol             sipAPI->api_export_symbol
#define sipImportSymbol             sipAPI->api_import_symbol
#define sipFindType                 sipAPI->api_find_type
#define sipBytes_AsChar             sipAPI->api_bytes_as_char
#define sipBytes_AsString           sipAPI->api_bytes_as_string
#define sipString_AsASCIIChar       sipAPI->api_string_as_ascii_char
#define sipString_AsASCIIString     sipAPI->api_string_as_ascii_string
#define sipString_AsLatin1Char      sipAPI->api_string_as_latin1_char
#define sipString_AsLatin1String    sipAPI->api_string_as_latin1_string
#define sipString_AsUTF8Char        sipAPI->api_string_as_utf8_char
#define sipString_AsUTF8String      sipAPI->api_string_as_utf8_string
#define sipUnicode_AsWChar          sipAPI->api_unicode_as_wchar
#define sipUnicode_AsWString        sipAPI->api_unicode_as_wstring
#define sipConvertFromConstVoidPtr  sipAPI->api_convert_from_const_void_ptr
#define sipConvertFromVoidPtrAndSize    sipAPI->api_convert_from_void_ptr_and_size
#define sipConvertFromConstVoidPtrAndSize   sipAPI->api_convert_from_const_void_ptr_and_size
#define sipWrappedTypeName(wt)      ((wt)->wt_td->td_cname)
#define sipGetReference             sipAPI->api_get_reference
#define sipKeepReference            sipAPI->api_keep_reference
#define sipRegisterPyType           sipAPI->api_register_py_type
#define sipTypeFromPyTypeObject     sipAPI->api_type_from_py_type_object
#define sipTypeScope                sipAPI->api_type_scope
#define sipResolveTypedef           sipAPI->api_resolve_typedef
#define sipRegisterAttributeGetter  sipAPI->api_register_attribute_getter
#define sipEnableAutoconversion     sipAPI->api_enable_autoconversion
#define sipInitMixin                sipAPI->api_init_mixin
#define sipExportModule             sipAPI->api_export_module
#define sipInitModule               sipAPI->api_init_module
#define sipGetInterpreter           sipAPI->api_get_interpreter
#define sipSetTypeUserData          sipAPI->api_set_type_user_data
#define sipGetTypeUserData          sipAPI->api_get_type_user_data
#define sipPyTypeDict               sipAPI->api_py_type_dict
#define sipPyTypeName               sipAPI->api_py_type_name
#define sipGetCFunction             sipAPI->api_get_c_function
#define sipGetMethod                sipAPI->api_get_method
#define sipFromMethod               sipAPI->api_from_method
#define sipGetDate                  sipAPI->api_get_date
#define sipFromDate                 sipAPI->api_from_date
#define sipGetDateTime              sipAPI->api_get_datetime
#define sipFromDateTime             sipAPI->api_from_datetime
#define sipGetTime                  sipAPI->api_get_time
#define sipFromTime                 sipAPI->api_from_time
#define sipIsUserType               sipAPI->api_is_user_type
#define sipCheckPluginForType       sipAPI->api_check_plugin_for_type
#define sipUnicodeNew               sipAPI->api_unicode_new
#define sipUnicodeWrite             sipAPI->api_unicode_write
#define sipUnicodeData              sipAPI->api_unicode_data
#define sipGetBufferInfo            sipAPI->api_get_buffer_info
#define sipReleaseBufferInfo        sipAPI->api_release_buffer_info
#define sipIsOwnedByPython          sipAPI->api_is_owned_by_python
#define sipIsDerivedClass           sipAPI->api_is_derived_class
#define sipGetUserObject            sipAPI->api_get_user_object
#define sipSetUserObject            sipAPI->api_set_user_object
#define sipRegisterEventHandler     sipAPI->api_register_event_handler
#define sipConvertToArray           sipAPI->api_convert_to_array
#define sipConvertToTypedArray      sipAPI->api_convert_to_typed_array
#define sipEnableGC                 sipAPI->api_enable_gc
#define sipPrintObject              sipAPI->api_print_object
#define sipLong_AsChar              sipAPI->api_long_as_char
#define sipLong_AsSignedChar        sipAPI->api_long_as_signed_char
#define sipLong_AsUnsignedChar      sipAPI->api_long_as_unsigned_char
#define sipLong_AsShort             sipAPI->api_long_as_short
#define sipLong_AsUnsignedShort     sipAPI->api_long_as_unsigned_short
#define sipLong_AsInt               sipAPI->api_long_as_int
#define sipLong_AsUnsignedInt       sipAPI->api_long_as_unsigned_int
#define sipLong_AsLong              sipAPI->api_long_as_long
#define sipLong_AsUnsignedLong      sipAPI->api_long_as_unsigned_long
#define sipLong_AsLongLong          sipAPI->api_long_as_long_long
#define sipLong_AsUnsignedLongLong  sipAPI->api_long_as_unsigned_long_long
#define sipLong_AsSizeT             sipAPI->api_long_as_size_t
#define sipVisitWrappers            sipAPI->api_visit_wrappers
#define sipRegisterExitNotifier     sipAPI->api_register_exit_notifier
#define sipGetFrame                 sipAPI->api_get_frame
#define sipDeprecated               sipAPI->api_deprecated
#define sipPyTypeDictRef            sipAPI->api_py_type_dict_ref
#define sipNextExceptionHandler     sipAPI->api_next_exception_handler
#define sipConvertToTypeUS          sipAPI->api_convert_to_type_us
#define sipForceConvertToTypeUS     sipAPI->api_force_convert_to_type_us
#define sipReleaseTypeUS            sipAPI->api_release_type_us
#define sipIsPyMethod               sipAPI->api_is_py_method
''')

        if self.py_enums_supported():
            sf.write(
f'''#define sipIsEnumFlag               sipAPI->api_is_enum_flag
''')

    def g_static_variables_table(self, sf, scope=None):
        """ Generate the table of static variables for a scope and return the
        length of the table.
        """

        c_bindings = self.spec.c_bindings

        nr_static_variables = 0

        # Get the sorted list of variables.
        variables = list(self.variables_in_scope(scope))
        variables.sort(key=lambda k: k.py_name.name)

        # Generate any getters and setters.
        for variable in variables:
            v_ref = variable.fq_cpp_name.as_word

            if variable.get_code is not None:
                # TODO Support sipPyType when scope is not None.
                # TODO Review the need to cache class instances (see legacy
                # variable handlers).  Or is that now in the sip module
                # wrapper?
                sf.write('\n')

                if not c_bindings:
                    sf.write(f'extern "C" {{static PyObject *sipStaticVariableGetter_{v_ref}();}}\n')

                sf.write(
f'''static PyObject *sipStaticVariableGetter_{v_ref}()
{{
    PyObject *sipPy;

''')

                sf.write_code(variable.get_code)

                sf.write(
'''
    return sipPy;
}

''')

            if variable.set_code is not None:
                # TODO Support sipPyType when scope is not None.
                sf.write('\n')

                if not c_bindings:
                    sf.write(f'extern "C" {{static int sipStaticVariableSetter_{v_ref}(PyObject *);}}\n')

                sf.write(
f'''static int sipStaticVariableSetter_{v_ref}(PyObject *sipPy)
{{
    int sipErr = 0;

''')

                sf.write_code(variable.set_code)

                sf.write(
'''
    return sipErr ? -1 : 0;
}

''')

        module = self.spec.module

        for variable in variables:
            v_type = variable.type

            # Generally const variables cannot be set.  However for string
            # pointers the reverse is true as a const pointer can be replaced
            # by another, but we can't allow a the contents of a non-const
            # string/array to be modified by C/C++ because they are immutable
            # in Python.
            not_settable = False
            might_need_key = False

            # TODO Unnamed enums, custom enums, Python enums and classes/mapped
            # types.
            if v_type.type is ArgumentType.CLASS or (v_type.type is ArgumentType.ENUM and v_type.definition.fq_cpp_name is not None):
                pass

            elif v_type.type is ArgumentType.BYTE:
                type_id = 'sipTypeID_byte'
                not_settable = v_type.is_const

            elif v_type.type is ArgumentType.SBYTE:
                type_id = 'sipTypeID_sbyte'
                not_settable = v_type.is_const

            elif v_type.type is ArgumentType.UBYTE:
                type_id = 'sipTypeID_ubyte'
                not_settable = v_type.is_const

            elif v_type.type is ArgumentType.SHORT:
                type_id = 'sipTypeID_short'
                not_settable = v_type.is_const

            elif v_type.type is ArgumentType.USHORT:
                type_id = 'sipTypeID_ushort'
                not_settable = v_type.is_const

            elif v_type.type in (ArgumentType.INT, ArgumentType.CINT):
                type_id = 'sipTypeID_int'
                not_settable = v_type.is_const

            elif v_type.type is ArgumentType.UINT:
                type_id = 'sipTypeID_uint'
                not_settable = v_type.is_const

            elif v_type.type is ArgumentType.LONG:
                type_id = 'sipTypeID_long'
                not_settable = v_type.is_const

            elif v_type.type is ArgumentType.ULONG:
                type_id = 'sipTypeID_ulong'
                not_settable = v_type.is_const

            elif v_type.type is ArgumentType.LONGLONG:
                type_id = 'sipTypeID_longlong'
                not_settable = v_type.is_const

            elif v_type.type is ArgumentType.ULONGLONG:
                type_id = 'sipTypeID_ulonglong'
                not_settable = v_type.is_const

            elif v_type.type is ArgumentType.HASH:
                type_id = 'sipTypeID_Py_hash_t'
                not_settable = v_type.is_const

            elif v_type.type is ArgumentType.SSIZE:
                type_id = 'sipTypeID_Py_ssize_t'
                not_settable = v_type.is_const

            elif v_type.type is ArgumentType.SIZE:
                type_id = 'sipTypeID_size_t'
                not_settable = v_type.is_const

            elif v_type.type in (ArgumentType.FLOAT, ArgumentType.CFLOAT):
                type_id = 'sipTypeID_float'
                not_settable = v_type.is_const

            elif v_type.type in (ArgumentType.DOUBLE, ArgumentType.CDOUBLE):
                type_id = 'sipTypeID_double'
                not_settable = v_type.is_const

            elif v_type.type is ArgumentType.STRING:
                if len(v_type.derefs) == 0:
                    type_id = 'sipTypeID_char'
                    not_settable = v_type.is_const
                else:
                    type_id = 'sipTypeID_str'
                    not_settable = not v_type.is_const
                    might_need_key = True

            elif v_type.type is ArgumentType.ASCII_STRING:
                if len(v_type.derefs) == 0:
                    type_id = 'sipTypeID_char_ascii'
                    not_settable = v_type.is_const
                else:
                    type_id = 'sipTypeID_str_ascii'
                    not_settable = not v_type.is_const
                    might_need_key = True

            elif v_type.type is ArgumentType.LATIN1_STRING:
                if len(v_type.derefs) == 0:
                    type_id = 'sipTypeID_char_latin1'
                    not_settable = v_type.is_const
                else:
                    type_id = 'sipTypeID_str_latin1'
                    not_settable = not v_type.is_const
                    might_need_key = True

            elif v_type.type is ArgumentType.UTF8_STRING:
                if len(v_type.derefs) == 0:
                    type_id = 'sipTypeID_char_utf8'
                    not_settable = v_type.is_const
                else:
                    type_id = 'sipTypeID_str_utf8'
                    not_settable = not v_type.is_const
                    might_need_key = True

            elif v_type.type is ArgumentType.SSTRING:
                if len(v_type.derefs) == 0:
                    type_id = 'sipTypeID_schar'
                    not_settable = v_type.is_const
                else:
                    type_id = 'sipTypeID_sstr'
                    not_settable = not v_type.is_const
                    might_need_key = True

            elif v_type.type is ArgumentType.USTRING:
                if len(v_type.derefs) == 0:
                    type_id = 'sipTypeID_uchar'
                    not_settable = v_type.is_const
                else:
                    type_id = 'sipTypeID_ustr'
                    not_settable = not v_type.is_const
                    might_need_key = True

            elif v_type.type is ArgumentType.WSTRING:
                if len(v_type.derefs) == 0:
                    type_id = 'sipTypeID_wchar'
                    not_settable = v_type.is_const
                else:
                    # Note that wchar_t strings/arrays are mutable.
                    type_id = 'sipTypeID_wstr'
                    might_need_key = True

            elif v_type.type in (ArgumentType.BOOL, ArgumentType.CBOOL):
                type_id = 'sipTypeID_bool'
                not_settable = v_type.is_const

            elif v_type.type is ArgumentType.VOID:
                # This is the only type that we need to make a distinction
                # between const and non-const (because if affects the behaviour
                # of a corresponding voidptr instance).  Using a flag
                # (potentially applicable to all types) would smell better but
                # we don't have anywhere to store it.  (SIP_SV_RO is a special
                # value rather than a flag).
                type_id = 'sipTypeID_voidptr_const' if v_type.is_const else 'sipTypeID_voidptr'

            elif v_type.type is ArgumentType.PYOBJECT:
                type_id = 'sipTypeID_pyobject'

            elif v_type.type is ArgumentType.PYTUPLE:
                type_id = 'sipTypeID_pytuple'

            elif v_type.type is ArgumentType.PYLIST:
                type_id = 'sipTypeID_pylist'

            elif v_type.type is ArgumentType.PYDICT:
                type_id = 'sipTypeID_pydict'

            elif v_type.type is ArgumentType.PYCALLABLE:
                type_id = 'sipTypeID_pycallable'

            elif v_type.type is ArgumentType.PYSLICE:
                type_id = 'sipTypeID_pyslice'

            elif v_type.type is ArgumentType.PYTYPE:
                type_id = 'sipTypeID_pytype'

            elif v_type.type is ArgumentType.PYBUFFER:
                type_id = 'sipTypeID_pybuffer'

            elif v_type.type is ArgumentType.CAPSULE:
                type_id = 'sipTypeID_pycapsule'

            else:
                continue

            if nr_static_variables == 0:
                if scope is None:
                    scope_type = 'module'
                    suffix = ''
                else:
                    scope_type = 'type'
                    suffix = '_' + scope.iface_file.fq_cpp_name.as_word

                sf.write(
f'''
/* Define the static variables for the {scope_type}. */
static const sipStaticVariableDef sipStaticVariablesTable{suffix}[] = {{
''')

            name = variable.py_name
            value = variable.fq_cpp_name.cpp_stripped(STRIP_GLOBAL)

            if not_settable or variable.no_setter:
                key = 'SIP_SV_RO'
            elif might_need_key:
                key = module.next_key
                module.next_key -= 1
            else:
                key = '0'

            v_ref = variable.fq_cpp_name.as_word
            getter = self.optional_ptr(variable.get_code is not None,
                    f'sipStaticVariableGetter_{v_ref}')
            setter = self.optional_ptr(variable.set_code is not None,
                    f'sipStaticVariableSetter_{v_ref}')

            sf.write(f'    {{"{name}", {type_id}, {key}, (void *)&{value}, {getter}, {setter}}},\n')

            nr_static_variables += 1

        if nr_static_variables != 0:
            sf.write('};\n')

        return nr_static_variables

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

    @staticmethod
    def cached_name_ref(cached_name, as_nr=False):
        """ Return a reference to a cached name. """

        # In v14 we always use the literal text.
        assert(not as_nr)

        return '"' + cached_name.name + '"'

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

    def py_enums_supported(self):
        """ Return True if Python enums are supported. """

        return SipModuleConfiguration.PyEnums in self.spec.sip_module_configuration

    @staticmethod
    def py_method_args(*, is_impl, is_method):
        """ Return the part of a Python method signature that are ABI
        dependent.
        """

        args = 'PyObject *'

        if is_method:
            args += ', PyTypeObject *'

            if is_impl:
                args += 'sipDefClass'
        else:
            args += 'sipModule'

        args += ', PyObject *const *'

        if is_impl:
            args += 'sipArgs'

        args += ', Py_ssize_t'

        if is_impl:
            args += ' sipNrArgs'

        return args

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

                py_name = member.py_name.name

                sf.write(f'        {{"{py_name}", SIP_MLMETH_CAST(func_{py_name}), METH_FASTCALL')

                if member.no_arg_parser or member.allow_keyword_args:
                    sf.write('|METH_KEYWORDS')

                docstring = self.optional_ptr(
                        has_member_docstring(bindings, member,
                                self.spec.module.overloads),
                        'doc_' + py_name)
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
