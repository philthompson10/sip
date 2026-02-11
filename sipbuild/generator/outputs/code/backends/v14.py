# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2026 Phil Thompson <phil@riverbankcomputing.com>


from .....sip_module_configuration import SipModuleConfiguration

from ....python_slots import is_number_slot, is_rich_compare_slot
from ....scoped_name import ScopedName, STRIP_GLOBAL
from ....specification import (Argument, ArgumentType, GILUse, IfaceFileType,
        MappedType, MultiInterpreterSupport, PySlot, WrappedEnum,
        WrappedVariable)

from ...formatters import fmt_class_as_scoped_py_name

from ..snippets import (g_class_docstring, g_class_method_table,
        g_module_docstring, g_py_slot, g_pyqt_class_plugin,
        g_pyqt_helper_defns, g_pyqt_helper_init, g_static_function,
        g_type_init_body)
from ..utils import (get_class_flags, get_enum_member, get_function_table,
        get_mapped_type_flags, get_optional_ptr, get_type_from_void,
        has_method_docstring, need_dealloc, py_scope, pyqt5_supported,
        pyqt6_supported, scoped_class_name, variables_in_scope)

from .abstract_backend import AbstractBackend


class v14Backend(AbstractBackend):
    """ The backend code generator for v14 of the ABI. """

    def g_conversion_to_enum(self, sf, enum):
        """ Generate the code to convert a Python enum (sipSelf) to a C/C++
        enum (sipCpp).
        """

        type_ref = self.get_type_ref(enum)
        cpp_name = enum.fq_cpp_name.as_cpp

        sf.write(
f'''
    {cpp_name} sipCpp;
    if (sipConvertToEnum(sipModule, sipSelf, &sipCpp, {type_ref}) < 0)
''')

    def g_cpp_dtor(self, sf):
        """ Generate the body of the dtor of a generated shadow class. """

        sf.write(
'''    if (sipPySelf)
        sipInstanceDestroyed(sipGetModule(sipPySelf), &sipPySelf);
''')

    def g_create_wrapped_module(self, sf, bindings,
        # TODO These will probably be generated here at some point.
        name_cache_state,
        has_external,
        enums_state,
        has_virtual_error_handlers,
        nr_subclass_convertors,
        static_variables_state,
        slot_extenders,
        init_extenders
    ):
        """ Generate the code to generate a wrapped module and return the
        enums state.
        """

        spec = self.spec
        target_abi = spec.target_abi
        module = spec.module
        module_name = module.py_name

        nr_static_variables, nr_types = static_variables_state

        # Generate the pointer to the immutable SIP ABI structure that is
        # obtained from the sip module.  It is the only static variable used
        # and is set when the wrapped module is first imported into an
        # interpreter.  If the module is imported into another interpreter then
        # it will be overwritten but always by the same value.  It would be
        # possible to keep this pointer in the module state but it could only
        # be obtained by first acquiring the GIL and there are calls in the ABI
        # that don't otherwise need the GIL (and so would be less performant
        # than older ABIs).
        sf.write(
f'''
/* The immutable SIP ABI implementation. */
const sipABISpec *sipABI_{module_name};


''')

        sf.write(

f'''/* The module's immutable specification. */
static const sipModuleSpec sipModule_{module_name} = {{
    .abi_major = {target_abi[0]},
    .abi_minor = {target_abi[1]},
    .sip_configuration = 0x{spec.sip_module_configuration:04x},
''')

        if len(module.all_imports) != 0:
            sf.write('    .imports = importsTable,\n')

        # TODO Exclude non-local types.  They are needed (ie. I think we need
        # the iface file) but we don't generated definition structures.
        if len(module.needed_types) != 0:
            sf.write(f'    .nr_type_specs = {len(module.needed_types)},\n')
            sf.write(f'    .type_specs = sipTypeSpecs_{module_name},\n')

        if has_external:
            sf.write('    .imports = externalTypesTable,\n')

        if module.nr_typedefs != 0:
            sf.write(f'    .nr_typedefs = {module.nr_typedefs},\n')
            sf.write('    .typedefs = typedefsTable,\n')

        if has_virtual_error_handlers:
            sf.write('    .virterrorhandlers = virtErrorHandlersTable,\n')

        if nr_subclass_convertors != 0:
            sf.write('    .convertors = convertorsTable,\n')

        if nr_static_variables != 0:
            sf.write(f'    .attributes.nr_static_variables = {nr_static_variables},\n')
            sf.write(f'    .attributes.static_variables = sipModuleVariables_{module_name},\n')

        if nr_types != 0:
            sf.write(f'    .attributes.nr_types = {nr_types},\n')
            sf.write(f'    .attributes.type_nrs = sipTypeNrs_{module_name},\n')

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

        g_module_docstring(sf, module)
        g_pyqt_helper_defns(sf, spec)

        self._g_module_clear(sf)
        self._g_module_exec(sf)
        self._g_module_free(sf)
        self._g_module_traverse(sf)
        has_module_functions = self.g_module_functions_table(sf, bindings,
                module)
        self.g_module_definition(sf, has_module_functions=has_module_functions)
        self.g_module_init_start(sf)

        if spec.sip_module:
            self._g_module_bootstrap(sf)
        else:
            sf.write(f'    sipABI_{module_name} = &sip_abi;\n\n')

        sf.write(f'    return sipModuleSlots_{module_name};\n}}\n')

        return enums_state

    def g_enums_specifications(self, sf, bindings, scope=None):
        """ Generate the specifications for the wrapped enums in a scope and
        return the optional dict of all enums defined in the module.
        """

        spec = self.spec
        module = spec.module
        module_name = module.py_name

        # If we are generating the specs at the module level then return a dict
        # of all enums (ie. including those defined with a scope) keyed by the
        # enum and with a value that is the index of the enum within the
        # scope's table of enums.
        if scope is None:
            enums_in_module = {}
            enum_nrs_by_scope = {}
        else:
            enums_in_module = None

        # Note that we go through the sorted table of needed types rather than
        # the unsorted list of all enums.
        enums_in_scope = []

        for needed_type in module.needed_types:
            if needed_type.type is not ArgumentType.ENUM:
                continue

            enum = needed_type.definition
            enum_py_scope = py_scope(enum.scope)

            # If required add the enum to the dict of all required enums even
            # we aren't generating a specification for it.
            if enums_in_module is not None:
                enum_nr = enum_nrs_by_scope.setdefault(enum_py_scope, 0)
                enums_in_module[enum] = enum_nr
                enum_nrs_by_scope[enum_py_scope] = enum_nr + 1

            if enum_py_scope is not scope:
                continue

            enums_in_scope.append(enum)

            # Generate the members table.
            sf.write(f'\nstatic const sipEnumMemberSpec sipEnumMembers_{module_name}_{enum.fq_cpp_name.as_word}[] = {{\n')

            for member in enum.members:
                name = str(member.py_name)
                value_field = self._get_enum_member_value_field(member)
                sf.write(f'    {{.name = "{name}", {value_field}}},\n')

            sf.write('    {}\n};\n')

        for enum in enums_in_scope:
            enum_name = enum.fq_cpp_name.as_word

            # Generate any enum slot tables.
            self.g_slot_implementations(sf, bindings, enum, enum.slots)

            slots_table = self._g_slots_table(sf, enum_name, enum.slots)

            if self.py_enums_supported():
                flags = 'SIP_TYPE_ENUM'
            else:
                flags = 'SIP_TYPE_SCOPED_ENUM' if enum.is_scoped else 'SIP_TYPE_ENUM'

            cpp_name = self.cached_name_ref(enum.cached_fq_cpp_name)

            if enum.enum_base_type is None:
                cpp_base_type_suffix = 'int'
            else:
                cpp_base_type_suffix = enum.enum_base_type.type.name.lower().replace('string', 'byte')

            if py_scope(enum.scope) is None:
                scope_nr = -1
                scope_name = module_name
            else:
                scope_nr = enum.scope.iface_file.type_nr

                if isinstance(enum.scope, MappedType):
                    scope_name = f'{module_name}.{enum.scope.py_name}'
                else:
                    scope_name = fmt_class_as_scoped_py_name(enum.scope)

            sf.write(
f'''
const sipEnumTypeSpec sipEnumTypeSpec_{module_name}_{enum_name} = {{
    .base.flags = {flags},
    .base.cpp_name = {cpp_name},
    .cpp_base_type = sipTypeID_{cpp_base_type_suffix},
    .fq_py_name = "{scope_name}.{enum.py_name}",
    .members = sipEnumMembers_{module_name}_{enum.fq_cpp_name.as_word},
    .scope_nr = {scope_nr},
''')

            if self.py_enums_supported():
                sf.write(
f'''    .py_base_type = SIP_ENUM_{enum.base_type.name},
''')

            if slots_table is not None:
                sf.write(f'    .py_slots = {slots_table},\n')

            sf.write('};\n')

        return enums_in_module

    def g_get_py_reimpl(self, sf, klass, overload, virt_nr):
        """ Generate the code to get the Python reimplementation of a C++
        virtual.
        """

        if overload.is_const:
            const_cast_char = 'const_cast<char *>('
            const_cast_po = 'const_cast<PyObject **>('
            const_cast_tail = ')'
        else:
            const_cast_char = ''
            const_cast_po = ''
            const_cast_tail = ''

        klass_py_name_ref = self.cached_name_ref(klass.py_name) if overload.is_abstract else 'SIP_NULLPTR'
        member_py_name_ref = self.cached_name_ref(overload.common.py_name)

        sf.write(
f'''    PyObject *sipModule;

    if (sipPySelf)
    {{
        sipModule = sipGetModule(sipPySelf);
        sipMeth = sipIsPyMethod(sipModule, &sipGILState, {const_cast_char}&sipPyMethods[{virt_nr}]{const_cast_tail}, {const_cast_po}&sipPySelf{const_cast_tail}, {klass_py_name_ref}, {member_py_name_ref});
    }}
    else
    {{
        sipModule = sipMeth = SIP_NULLPTR;
    }}
''')

    def g_init_mixin_impl_body(self, sf, klass):
        """ Generate the body of the implementation of a mixin initialisation
        function.
        """

        self.g_slot_support_vars(sf, klass, None)

        type_ref = self.get_type_ref(klass)

        sf.write(f'    return sipInitMixin(sipModule, sipSelf, sipArgs, sipKwds, {type_ref});\n')

    def g_mapped_type_api(self, sf, mapped_type):
        """ Generate the API details for a mapped type. """

        module_name = self.spec.module.py_name
        iface_file = mapped_type.iface_file
        mapped_type_name = iface_file.fq_cpp_name.as_word

        sf.write(
f'''
#define {self.get_type_ref(mapped_type)} SIP_TYPE_ID_TYPE_MAPPED|SIP_TYPE_ID_CURRENT_MODULE|{iface_file.type_nr}

extern sipMappedTypeSpec sipTypeSpec_{module_name}_{mapped_type_name};
''')

    def g_mapped_type_definition(self, sf, bindings, mapped_type):
        """ Generate the type structure that contains all the information
        needed by a mapped type.
        """

        spec = self.spec
        module_name = mapped_type.iface_file.module.py_name
        mapped_type_name = mapped_type.iface_file.fq_cpp_name.as_word

        fields = []

        # Generate the enums table.
        self.g_enums_specifications(sf, bindings, scope=mapped_type)

        # Generate the slots table.
        slots_table = self._g_slots_table(sf, mapped_type_name,
                mapped_type.members)

        # Generate the methods table.
        nr_methods = self.g_py_method_table(sf, bindings,
                get_function_table(mapped_type.members), mapped_type)

        # Generate the static variables (ie. enum types) table.
        _, nr_types = self.g_static_variables_table(sf, scope=mapped_type)

        fields.append('.base.flags = ' + get_mapped_type_flags(mapped_type))
        fields.append('.base.cpp_name = ' + self.cached_name_ref(mapped_type.cpp_name))

        if pyqt6_supported(spec) and mapped_type.pyqt_flags != 0:
            # TODO
            #sf.write(f'\n\nstatic pyqt6MappedTypePluginDef plugin_{mapped_type_name} = {{{mapped_type.pyqt_flags}}};\n')

            fields.append('.base.plugin_data = &plugin_' + mapped_type_name)

        if nr_types != 0 or nr_methods != 0:
            fields.append(
                    f'.container.fq_py_name = "{module_name}.{mapped_type.py_name}"')

        if slots_table is not None:
            fields.append('.container.py_slots = ' + slots_table)

        if nr_types != 0:
            fields.append(
                    '.container.attributes.nr_types = ' + str(nr_types))
            fields.append(
                    '.container.attributes.type_nrs = sipTypeNrs_' + mapped_type_name)

        if nr_methods != 0:
            fields.append(
                    '.container.methods = sipMethods_' + mapped_type_name)

        if not mapped_type.no_assignment_operator:
            fields.append('.assign = assign_' + mapped_type_name)

        if not mapped_type.no_default_ctor:
            fields.append('.array = array_' + mapped_type_name)

        if not mapped_type.no_copy_ctor:
            fields.append('.copy = copy_' + mapped_type_name)

        if not mapped_type.no_release:
            fields.append('.release = release_' + mapped_type_name)

        if mapped_type.convert_to_type_code is not None:
            fields.append('.cto = convertTo_' + mapped_type_name)

        if mapped_type.convert_from_type_code is not None:
            fields.append('.cfrom = convertFrom_' + mapped_type_name)

        fields = ',\n    '.join(fields)

        sf.write(
f'''

sipMappedTypeSpec sipTypeSpec_{module_name}_{mapped_type_name} = {{
    {fields}
}};
''')

    def g_method_support_vars(self, sf):
        """ Generate the variables needed by a method. """

        sf.write('    PyObject *sipModule = PyType_GetModule(sipDefType);\n')

    # Map GILUse values.
    _MAP_GIL_USED = {
        GILUse.USED:        'Py_MOD_GIL_USED',
        GILUse.NOT_USED:    'Py_MOD_GIL_NOT_USED',
    }

    # Map MultiInterpreterSupport values.
    _MAP_MULTI_INTERPRETER_SUPPORT = {
        MultiInterpreterSupport.NOT_SUPPORTED:
                'Py_MOD_MULTIPLE_INTERPRETERS_NOT_SUPPORTED',
        MultiInterpreterSupport.PER_INTERPRETER_GIL_SUPPORTED:
                'Py_MOD_PER_INTERPRETER_GIL_SUPPORTED',
        MultiInterpreterSupport.SUPPORTED:
                'Py_MOD_MULTIPLE_INTERPRETERS_SUPPORTED',
    }

    def g_module_definition(self, sf, has_module_functions=False):
        """ Generate the module definition structure. """

        spec = self.spec
        module = spec.module

        gil_support = self._MAP_GIL_USED[module.gil_use]
        interp_support = self._MAP_MULTI_INTERPRETER_SUPPORT[module.multi_interpreter_support]

        if spec.sip_module:
            state_size = '0'
        else:
            state_size = '(void *)sizeof (sipModuleState)'
            sf.write(
'''

#include "sip_core.h"
#include "sip_wrapped_module.h"
''')

        # Note that the sip module implementation expects Py_mod_name and
        # Py_mod_state_size to be the first and second slots respectively.
        sf.write(
f'''

/* The module's immutable slot definitions. */
PyABIInfo_VAR(sip_abi_info);

PyModuleDef_Slot sipModuleSlots_{module.py_name}[] = {{
    {{Py_mod_name, (void *)"{module.fq_py_name}"}},
    {{Py_mod_state_size, {state_size}}},
    {{Py_mod_abi, &sip_abi_info}},
    {{Py_mod_exec, (void *)module_exec}},
    {{Py_mod_gil, {gil_support}}},
    {{Py_mod_multiple_interpreters, {interp_support}}},
    {{Py_mod_state_clear, (void *)module_clear}},
    {{Py_mod_state_free, (void *)module_free}},
    {{Py_mod_state_traverse, (void *)module_traverse}},
''')

        if module.docstring is not None:
            # TODO The name should have a sip_ prefix.
            sf.write(f'    {{Py_mod_doc, (void *)doc_mod_{module.py_name}}},\n')

        if has_module_functions:
            sf.write(f'    {{Py_mod_methods, sip_methods_{module.py_name}}},\n')

        sf.write('    {0}\n};\n')

    def g_module_functions_table(self, sf, bindings, module):
        """ Generate the table of module functions and return True if anything
        was actually generated.
        """

        has_module_functions = self._g_module_function_table_entries(sf,
                bindings, module, module.global_functions)

        # Generate the module functions for any hidden namespaces.
        for klass in self.spec.classes:
            if klass.iface_file.module is module and klass.is_hidden_namespace:
                has_module_functions = self._g_module_function_table_entries(
                        sf, bindings, module, klass.members,
                        has_module_functions=has_module_functions)

        if has_module_functions:
            sf.write(
'''    {}
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

/* The module's export function. */
#if defined(SIP_STATIC_MODULE)
{extern_c}PyObject *PyModExport_{module_name}({arg_type})
#else
PyMODEXPORT_FUNC PyModExport_{module_name}({arg_type})
#endif
{{
''')

    @staticmethod
    def g_not_implemented(sf):
        """ Generate the code to clear any exception and return
        Py_NotImplemented.
        """

        sf.write(
'''
    PyErr_Clear();

    return Py_NewRef(Py_NotImplemented);
''')

    def g_py_method_table(self, sf, bindings, members, scope):
        """ Generate a Python method table for a class or mapped type and
        return the number of entries.
        """

        scope_name = scope.iface_file.fq_cpp_name.as_word

        no_intro = True

        for member_nr, member in enumerate(members):
            # Save the index in the table.
            member.member_nr = member_nr

            py_name = member.py_name
            cached_py_name = self.cached_name_ref(py_name)

            docstring = get_optional_ptr(
                    has_method_docstring(bindings, member, scope.overloads),
                    f'doc_{scope_name}_{py_name.name}')

            if no_intro:
                sf.write(
f'''

static PyMethodDef sipMethods_{scope_name}[] = {{
''')

                no_intro = False

            sf.write(f'    {{{cached_py_name}, SIP_MLMETH_CAST(meth_{scope_name}_{py_name.name}), METH_METHOD|METH_FASTCALL|METH_KEYWORDS, {docstring}}},\n')

        if not no_intro:
            sf.write('    {}\n};\n')

        return len(members)

    def g_sip_api(self, sf, module_name, module_state):
        """ Generate the SIP API as seen by generated code. """

        # TODO These have been reviewed as part of the public v14 API.
        # TODO Make sure sipGetModule() is documented as part of the public
        # API (if appropriate).
        sf.write(
f'''

/* The immutable SIP ABI. */
extern const sipABISpec *sipABI_{module_name};

extern PyModuleDef_Slot sipModuleSlots_{module_name}[];
#define sipGetModule(self)          PyType_GetModuleByToken(Py_TYPE(self), sipModuleSlots_{module_name})

#define sipModuleClear              sipABI_{module_name}->api_module_clear
#define sipModuleExec               sipABI_{module_name}->api_module_exec
#define sipModuleFree               sipABI_{module_name}->api_module_free
#define sipModuleTraverse           sipABI_{module_name}->api_module_traverse

#define sipBuildResult              sipABI_{module_name}->api_build_result
#define sipCallMethod               sipABI_{module_name}->api_call_method
#define sipCallProcedureMethod      sipABI_{module_name}->api_call_procedure_method
#define sipCanConvertToType         sipABI_{module_name}->api_can_convert_to_type
#define sipConvertFromEnum          sipABI_{module_name}->api_convert_from_enum
#define sipConvertFromNewType       sipABI_{module_name}->api_convert_from_new_type
#define sipConvertFromType          sipABI_{module_name}->api_convert_from_type
#define sipConvertToEnum            sipABI_{module_name}->api_convert_to_enum
#define sipConvertToType            sipABI_{module_name}->api_convert_to_type
#define sipFindTypeID               sipABI_{module_name}->api_find_type_id
#define sipForceConvertToType       sipABI_{module_name}->api_force_convert_to_type
#define sipGetAddress               sipABI_{module_name}->api_get_address
#define sipGetPyType                sipABI_{module_name}->api_get_py_type
#define sipGetState                 sipABI_{module_name}->api_get_state
#define sipGetTypeUserData          sipABI_{module_name}->api_get_type_user_data
#define sipIsOwnedByPython          sipABI_{module_name}->api_is_owned_by_python
#define sipParseResult              sipABI_{module_name}->api_parse_result
#define sipReleaseType              sipABI_{module_name}->api_release_type
#define sipReleaseTypeUS            sipABI_{module_name}->api_release_type_us
#define sipSetTypeUserData          sipABI_{module_name}->api_set_type_user_data
''')

        # TODO These have been reviewed as part of the private v14 API.
        sf.write(
f'''#define sipGetCppPtr                sipABI_{module_name}->api_get_cpp_ptr
#define sipInitMixin                sipABI_{module_name}->api_init_mixin
#define sipInstanceDestroyed        sipABI_{module_name}->api_instance_destroyed
#define sipIsDerivedClass           sipABI_{module_name}->api_is_derived_class
#define sipIsPyMethod               sipABI_{module_name}->api_is_py_method
#define sipNoFunction               sipABI_{module_name}->api_no_function
#define sipNoMethod                 sipABI_{module_name}->api_no_method
#define sipParseArgs                sipABI_{module_name}->api_parse_args
#define sipParseKwdArgs             sipABI_{module_name}->api_parse_kwd_args
#define sipParseVectorcallArgs      sipABI_{module_name}->api_parse_vectorcall_args
#define sipParseVectorcallKwdArgs   sipABI_{module_name}->api_parse_vectorcall_kwd_args
#define sipParsePair                sipABI_{module_name}->api_parse_pair
#define sipPySlotExtend             sipABI_{module_name}->api_py_slot_extend
''')

        # TODO These have yet to be reviewed.
        sf.write(
f'''#define sipMalloc                   sipAPI->api_malloc
#define sipFree                     sipAPI->api_free
#define sipCallErrorHandler         sipAPI->api_call_error_handler
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
#define sipGetMixinAddress          sipAPI->api_get_mixin_address
#define sipCallHook                 sipAPI->api_call_hook
#define sipEndThread                sipAPI->api_end_thread
#define sipRaiseUnknownException    sipAPI->api_raise_unknown_exception
#define sipRaiseTypeException       sipAPI->api_raise_type_exception
#define sipBadLengthForSlice        sipAPI->api_bad_length_for_slice
#define sipAddTypeInstance          sipAPI->api_add_type_instance
#define sipAddDelayedDtor           sipAPI->api_add_delayed_dtor
#define sipConvertToBool            sipAPI->api_convert_to_bool
#define sipConvertFromNewPyType     sipAPI->api_convert_from_new_pytype
#define sipExportSymbol             sipAPI->api_export_symbol
#define sipImportSymbol             sipAPI->api_import_symbol
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
#define sipWrappedTypeName(wt)      ((wt)->wt_td->cpp_name)
#define sipGetReference             sipAPI->api_get_reference
#define sipKeepReference            sipAPI->api_keep_reference
#define sipRegisterPyType           sipAPI->api_register_py_type
#define sipTypeFromPyTypeObject     sipAPI->api_type_from_py_type_object
#define sipTypeScope                sipAPI->api_type_scope
#define sipResolveTypedef           sipAPI->api_resolve_typedef
#define sipEnableAutoconversion     sipAPI->api_enable_autoconversion
#define sipExportModule             sipAPI->api_export_module
#define sipInitModule               sipAPI->api_init_module
#define sipGetInterpreter           sipAPI->api_get_interpreter
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
''')

        if self.py_enums_supported():
            sf.write(
f'''#define sipIsEnumFlag               sipAPI->api_is_enum_flag
''')

        # Generate the declarations of the individual scope enum specification
        # tables.
        need_intro = True
        all_module_enums = module_state

        for enum in all_module_enums:
            # Ignore global enums.
            if py_scope(enum.scope) is None:
                continue

            if need_intro:
                sf.write('\n\n/* Declare the enum specifications for each scope. */\n')
                need_intro = False

            sf.write(f'extern const sipEnumTypeSpec sipEnumTypeSpec_{module_name}_{enum.fq_cpp_name.as_word};\n')

    def g_slot_implementations(self, sf, bindings, scope, members):
        """ Generate the slot implementations for a scope. """

        if isinstance(scope, WrappedEnum):
            is_ns = False
            as_word = scope.fq_cpp_name.as_word
        else:
            is_ns = scope.iface_file.type is IfaceFileType.NAMESPACE
            as_word = scope.iface_file.fq_cpp_name.as_word

        has_getitem_slot = has_setitem_slot = has_delitem_slot = False
        rich_comparison_members = []

        for member in members:
            if is_ns:
                g_static_function(self, sf, bindings, member, scope=scope)
            elif member.py_slot is not None:
                g_py_slot(self, sf, bindings, member, scope=scope)

                if member.py_slot is PySlot.GETITEM:
                    has_getitem_slot = True

                if member.py_slot is PySlot.SETITEM:
                    has_setitem_slot = True

                if member.py_slot is PySlot.DELITEM:
                    has_delitem_slot = True

                if is_rich_compare_slot(member.py_slot):
                    rich_comparison_members.append(member)

        # Generate item dispatchers if required.
        if has_getitem_slot:
            sf.write(
f'''

extern "C" {{static PyObject *slot_{as_word}___sq_item__(PyObject *, Py_ssize_t);}}
static PyObject *slot_{as_word}___sq_item__(PyObject *self, Py_ssize_t n)
{{
    PyObject *arg = PyLong_FromSsize_t(n);
    if (arg == NULL)
        return NULL;

    PyObject *res = slot_{as_word}___getitem__(self, arg);
    Py_DECREF(arg);

    return res;
}}
''')

        if has_setitem_slot or has_delitem_slot:
            sf.write(
f'''

extern "C" {{static int slot_{as_word}___mp_ass_subscript__(PyObject *, PyObject *, PyObject *);}}
static int slot_{as_word}___mp_ass_subscript__(PyObject *self, PyObject *key, PyObject *value)
{{
    if (value != NULL)
''')

            if has_setitem_slot:
                sf.write(
f'''        return slot_{as_word}___setitem__(self, key, value);
''')
            else:
                sf.write(
'''    {
        PyErr_SetNone(PyExc_NotImplementedError);
        return -1;
    }
''')

            sf.write('\n');

            if has_delitem_slot:
                sf.write(
f'''    return slot_{as_word}___delitem__(self, key);
''')
            else:
                sf.write(
'''    PyErr_SetNone(PyExc_NotImplementedError);
    return -1;
''')

            sf.write('}\n');

            sf.write(
f'''

extern "C" {{static int slot_{as_word}___sq_ass_item__(PyObject *, Py_ssize_t, PyObject *);}}
static int slot_{as_word}___sq_ass_item__(PyObject *self, Py_ssize_t index, PyObject *value)
{{
    PyObject *key = PyLong_FromSsize_t(index);
    if (key == NULL)
        return -1;

    int res = slot_{as_word}___mp_ass_subscript__(self, key, value);

    Py_DECREF(key);

    return res;
}}
''');

        # Generate a rich comparision dispatcher if required.
        if rich_comparison_members:
            sf.write(
f'''

extern "C" {{static PyObject *slot_{as_word}___richcompare__(PyObject *, PyObject *, int);}}
static PyObject *slot_{as_word}___richcompare__(PyObject *self, PyObject *arg, int op)
{{
    switch (op)
    {{
''')

            for rc_member in rich_comparison_members:
                sf.write(f'    case Py_{rc_member.py_slot.name}: return slot_{as_word}_{rc_member.py_name}(self, arg);\n')

            sf.write(
f'''    }}

    return Py_NewRef(Py_NotImplemented);
}}
''')

    def g_slot_support_vars(self, sf, scope, member):
        """ Generate the variables needed by a slot function. """

        if isinstance(scope, WrappedEnum) and self.py_enums_supported:
            # Python enums are not extension types and so the defining module
            # has to be obtained from sys.modules.  This use case is very rare
            # and so we don't bother about the efficiency or otherwise.
            sf.write(
f'''    PyObject *sipModule = PyDict_GetItemString(PySys_GetObject("modules"), "{self.spec.module.py_name}");
    Py_DECREF(sipModule);
''')
        else:
            name = 'sipArg0' if member is not None and is_number_slot(member.py_slot) else 'sipSelf'
            sf.write(f'    PyObject *sipModule = sipGetModule({name});\n')

    def g_static_variables_table(self, sf, scope=None):
        """ Generate the tables of static variables and types for a scope and
        return a 2-tuple of the length of each table.
        """

        module = self.spec.module

        # Do the variables.
        nr_variables = self._g_variables_table(sf, scope, for_unbound=True)

        # Do the wrapped types.  First create a list of 2-tuples of Python name
        # and type number.
        type_nrs = []

        for needed_type in module.needed_types:
            if needed_type.type is ArgumentType.CLASS:
                klass = needed_type.definition

                if py_scope(klass.scope) is not scope or klass.external:
                    continue

                py_name = str(klass.py_name)
                type_nr = klass.iface_file.type_nr

            elif needed_type.type is ArgumentType.MAPPED:
                mapped_type = needed_type.definition

                if scope is not None or mapped_type.py_name is None:
                    continue

                py_name = str(mapped_type.py_name)
                type_nr = mapped_type.iface_file.type_nr

            elif needed_type.type is ArgumentType.ENUM:
                enum = needed_type.definition

                if py_scope(enum.scope) is not scope:
                    continue

                py_name = str(enum.py_name)
                type_nr = enum.type_nr

            type_nrs.append((py_name, type_nr))

        if type_nrs:
            type_nrs.sort(key=lambda tup: tup[0])

            suffix = module.py_name if scope is None else scope.iface_file.fq_cpp_name.as_word

            sf.write(f'\nstatic const sipTypeNr sipTypeNrs_{suffix}[] = {{\n    ')
            sf.write(', '.join([str(n) for _, n in type_nrs]))
            sf.write('\n};\n')

        return nr_variables, len(type_nrs)

    def g_type_definition(self, sf, bindings, klass, py_debug):
        """ Generate the type structure that contains all the information
        needed by the meta-type.  A sub-set of this is used to extend
        namespaces.
        """

        spec = self.spec
        module = spec.module
        module_name = module.py_name
        klass_name = klass.iface_file.fq_cpp_name.as_word

        # Generate the enums table.
        self.g_enums_specifications(sf, bindings, scope=klass)

        # Generate the slots table.
        slots_table = self._g_slots_table(sf, klass_name, klass.members,
                mixin=klass.mixin)

        # Generate the methods table.
        nr_methods = g_class_method_table(self, sf, bindings, klass)

        # Generate the static variables table.
        nr_static_variables, nr_types = self.g_static_variables_table(sf,
                scope=klass)

        # Generate the instance variables table.
        nr_instance_variables = self._g_variables_table(sf, scope=klass,
                for_unbound=False)

        # Generate the docstring.
        docstring_ref = g_class_docstring(sf, spec, bindings, klass)

        # Generate the type definition itself.
        fields = []

        fields.append(
                '.base.flags = ' + get_class_flags(spec, klass, py_debug))
        fields.append(
                '.base.cpp_name = ' + self.cached_name_ref(klass.iface_file.cpp_name))

        if pyqt5_supported(spec) or pyqt6_supported(spec):
            if g_pyqt_class_plugin(self, sf, bindings, klass):
                fields.append(
                        '.base.plugin_data = &plugin_' + klass_name)

        if klass.real_class is None:
            fields.append(
                    f'.container.fq_py_name = "{fmt_class_as_scoped_py_name(klass)}"')

        if klass.real_class is not None:
            scope_id = self.get_type_ref(klass.real_class)
        elif py_scope(klass.scope) is not None:
            scope_id = self.get_type_ref(klass.scope)
        else:
            scope_id = None

        if scope_id is not None:
            fields.append('.container.scope_id = ' + scope_id)

        if nr_methods != 0:
            fields.append(
                    '.container.methods = sipMethods_' + klass_name)

        if nr_instance_variables != 0:
            fields.append(
                    '.container.instance_variables = sipInstanceVariables_' + klass_name)

        if nr_static_variables != 0:
            fields.append(
                    '.container.attributes.nr_static_variables = ' + str(nr_static_variables))
            fields.append(
                    '.container.attributes.static_variables = sipStaticVariables_' + klass_name)

        if nr_types != 0:
            fields.append(
                    '.container.attributes.nr_types = ' + str(nr_types))
            fields.append(
                    '.container.attributes.type_nrs = sipTypeNrs_' + klass_name)

        if slots_table is not None:
            fields.append('.container.py_slots = ' + slots_table)

        fields.append('.docstring = ' + docstring_ref)

        if klass.metatype is not None:
            fields.append(
                    '.metatype = ' + self.cached_name_ref(klass.metatype))

        if klass.supertype is not None:
            fields.append(
                    '.supertype = ' + self.cached_name_ref(klass.supertype))


        # TODO
        #if len(klass.superclasses) != 0:
        #    supers

        if klass.can_create:
            fields.append('.init = init_type_' + klass_name)

        if need_dealloc(spec, bindings, klass):
            fields.append('.dealloc = dealloc_' + klass_name)

        if klass.can_create:
            fields.append(
                    f'.sizeof_class = sizeof ({scoped_class_name(self.spec, klass)})')

        if klass.gc_traverse_code is not None:
            fields.append('.traverse = traverse_' + klass_name)

        if klass.gc_clear_code is not None:
            fields.append('.clear = clear_' + klass_name)

        if klass.bi_get_buffer_code is not None:
            fields.append('.getbuffer = getbuffer_' + klass_name)

        if klass.bi_release_buffer_code is not None:
            fields.append('.releasebuffer = releasebuffer_' + klass_name)

        if spec.c_bindings or klass.needs_copy_helper:
            fields.append('.assign = assign_' + klass_name)

        if spec.c_bindings or klass.needs_array_helper:
            fields.append('.array = array_' + klass_name)

        if spec.c_bindings or klass.needs_copy_helper:
            fields.append('.copy = copy_' + klass_name)

        if not spec.c_bindings and klass.iface_file.type is not IfaceFileType.NAMESPACE:
            fields.append('.release = release_' + klass_name)

        if len(klass.superclasses) != 0:
            fields.append('.cast = cast_' + klass_name)

        if klass.convert_to_type_code is not None and klass.iface_file.type is not IfaceFileType.NAMESPACE:
            fields.append('.cto = convertTo_' + klass_name)

        if klass.convert_from_type_code is not None and klass.iface_file.type is not IfaceFileType.NAMESPACE:
            fields.append('.cfrom = convertFrom_' + klass_name)

        if klass.pickle_code is not None:
            fields.append('.pickle = pickle_' + klass_name)

        if klass.finalisation_code is not None:
            fields.append('.final = final_' + klass_name)

        if spec.c_bindings or klass.needs_array_helper:
            fields.append('.array_delete = array_delete_' + klass_name)

        fields = ',\n    '.join(fields)

        sf.write(
f'''

sipClassTypeSpec sipTypeSpec_{module_name}_{klass_name} = {{
    {fields}
}};
''')

    def g_type_init(self, sf, bindings, klass, need_self, need_owner):
        """ Generate the code that initialises a type. """

        spec = self.spec
        klass_name = klass.iface_file.fq_cpp_name.as_word

        if not spec.c_bindings:
            sf.write(f'extern "C" {{static void *init_type_{klass_name}(PyObject *, PyObject *const *, Py_ssize_t, PyObject *, PyObject **, PyObject **, PyObject **);}}\n')

        sip_owner = 'sipOwner' if need_owner else ''

        sf.write(
f'''static void *init_type_{klass_name}(PyObject *sipSelf, PyObject *const *sipArgs, Py_ssize_t sipNrArgs, PyObject *sipKwds, PyObject **sipUnused, PyObject **{sip_owner}, PyObject **sipParseErr)
{{
''')

        self.g_slot_support_vars(sf, klass, None)

        g_type_init_body(self, sf, bindings, klass)

        sf.write('}\n')

    @staticmethod
    def cached_name_ref(cached_name, as_nr=False):
        """ Return a reference to a cached name. """

        # In v14 we always use the literal text.
        assert(not as_nr)

        return '"' + cached_name.name + '"'

    def custom_enums_supported(self):
        """ Return True if custom enums are supported. """

        return SipModuleConfiguration.CustomEnums in self.spec.sip_module_configuration

    def get_class_ref_value(self, klass):
        """ Return the value of a class's reference. """

        return f'SIP_TYPE_ID_TYPE_CLASS|SIP_TYPE_ID_CURRENT_MODULE|{klass.iface_file.type_nr}'

    def get_enum_to_py_conversion(self, enum, value_name):
        """ Return the code to convert a C/C++ enum to a Python object. """

        if enum.fq_cpp_name is None:
            # TODO: This needs to support larger types and unsigned types.
            return f'PyLong_FromLong({value_name})'

        return f'sipConvertFromEnum(sipModule, &{value_name}, {self.get_type_ref(enum)})'

    def get_enum_ref_value(self, enum):
        """ Return the value of an enum's reference. """

        module_nr = 'SIP_TYPE_ID_CURRENT_MODULE' if enum.module is self.spec.module else enum.module.module_nr

        return f'SIP_TYPE_ID_TYPE_ENUM|{module_nr}|{enum.type_nr}'

    @staticmethod
    def get_module_context():
        """ Return the value of a module context passed as the first argument
        to many ABI calls.
        """

        return 'sipModule, '

    @staticmethod
    def get_module_context_decl():
        """ Return the declaration of the value of a module context passed as
        the first argument to many ABI calls.
        """

        return 'PyObject *sipModule, '

    def get_py_method_args(self, *, is_impl, is_module_fn, need_self=False,
            need_args=True):
        """ Return the part of a Python method signature that are ABI
        dependent.
        """

        if is_impl:
            if is_module_fn:
                args = 'PyObject *sipModule'
            else:
                args = 'PyObject *sipSelf, PyTypeObject *sipDefType'
        else:
            if is_module_fn:
                args = 'PyObject *'
            else:
                args = 'PyObject *, PyTypeObject *'

        args += ', PyObject *const *'

        if is_impl and need_args:
            args += 'sipArgs'

        args += ', Py_ssize_t'

        if is_impl and need_args:
            args += ' sipNrArgs'

        return args

    @staticmethod
    def get_result_parser():
        """ Return the name of the Python reimplementation result parser. """

        return 'sipParseResult'

    def get_sipself_test(self, klass):
        """ Return the code that checks if 'sipSelf' was bound or passed as an
        argument.
        """

        return f'(!PyObject_TypeCheck(sipSelf, sipGetPyType(sipModule, {self.get_type_ref(klass)})) || sipIsDerivedClass(sipSelf))'

    @staticmethod
    def get_slot_ref(slot_type):
        """ Return a reference to a slot. """

        if is_rich_compare_slot(slot_type):
            return 'Py_tp_richcompare'

        return _SLOT_ID_MAP[slot_type]

    def get_spec_for_class(self, klass):
        """ Return the name of the data structure specifying a class. """

        return f'sipTypeSpec_{self.spec.module.py_name}_{klass.iface_file.fq_cpp_name.as_word}.base'

    def get_spec_for_mapped_type(self, mapped_type):
        """ Return the name of the data structure specifying a mapped type. """

        return f'sipTypeSpec_{self.spec.module.py_name}_{mapped_type.iface_file.fq_cpp_name.as_word}.base'

    def get_spec_for_enum(self, enum, enums_state):
        """ Return the name of the data structure specifying an enum. """

        return f'sipEnumTypeSpec_{self.spec.module.py_name}_{enum.fq_cpp_name.as_word}.base'

    @staticmethod
    def get_spec_suffix():
        """ Return the suffix used for immutable specifications. """

        return 'Spec'

    @staticmethod
    def get_type_ref(wrapped_object):
        """ Return the reference to the type of a wrapped object. """

        fq_cpp_name = wrapped_object.fq_cpp_name if isinstance(wrapped_object, WrappedEnum) else wrapped_object.iface_file.fq_cpp_name

        return 'sipTypeID_' + fq_cpp_name.as_word

    @staticmethod
    def get_types_table_decl(module):
        """ Return the declaration of a module's wrapped types table. """

        return f'static const sipTypeSpec *const sipTypeSpecs_{module.py_name}'

    @staticmethod
    def get_wrapper_type():
        """ Return the type of the C representation of a wrapped object. """

        return 'PyObject *'

    @staticmethod
    def get_wrapper_type_cast():
        """ Return the cast from a PyObject* of the C representation of a
        wrapped object.
        """

        return ''

    def py_enums_supported(self):
        """ Return True if Python enums are supported. """

        return SipModuleConfiguration.PyEnums in self.spec.sip_module_configuration

    def _g_module_bootstrap(self, sf):
        """ Generate the module bootstrap code. """

        spec = self.spec
        module_name = spec.module.py_name

        sf.write(
f'''    PyObject *sip_module = PyImport_ImportModule("{spec.sip_module}");
    if (sip_module == NULL)
        return NULL;

    PyObject *capsule = PyObject_GetAttrString(sip_module, "_C_BOOTSTRAP");
    if (capsule == NULL)
    {{
        Py_DECREF(sip_module);
        return NULL;
    }}

    if (!PyCapsule_IsValid(capsule, "_C_BOOTSTRAP"))
    {{
        Py_DECREF(capsule);
        Py_DECREF(sip_module);
        return NULL;
    }}

    /*
     * The first stage of the bootstrap is to get a function that will be
     * called with the ABI version as its only argument and will return the
     * corresponding SIP ABI implementation.
     */
    sipBootstrapFunc bootstrap_func = (sipBootstrapFunc)PyCapsule_GetPointer(
            capsule, "_C_BOOTSTRAP");

    Py_DECREF(capsule);
    Py_DECREF(sip_module);

    if (bootstrap_func == NULL)
        return NULL;

    /*
     * The second stage of the bootstrap is to call the function from the first
     * stage to get the SIP ABI implementation (or NULL if it is not
     * supported).
     */
    sipABI_{module_name} = bootstrap_func({spec.target_abi[0]});
    if (sipABI_{module_name} == NULL)
        return NULL;

    /* Set the wrapped module state size from the sip module. */
    sipModuleSlots_{module_name}[1].value = (void *)sipABI_{module_name}->module_state_size;

''')

    @staticmethod
    def _g_module_clear(sf):
        """ Generate the module clear slot. """

        sf.write(
'''

/* The module's clear slot. */
static int module_clear(PyObject *mod)
{
    return sipModuleClear(mod);
}
''')

    def _g_module_exec(self, sf):
        """ Generate the module exec slot. """

        spec = self.spec
        module = spec.module
        module_name = module.py_name

        sf.write(
'''

/* The module's exec function. */
static int module_exec(PyObject *sipModule)
{
''')

        sf.write_code(module.preinitialisation_code)

        if spec.sip_module:
            sip_init_func_ref = 'sipModuleExec'
        else:
            sip_init_func_ref = 'sip_api_module_exec';

        sf.write_code(module.initialisation_code)

        g_pyqt_helper_init(sf, spec)

        sf.write(
f'''    if ({sip_init_func_ref}(sipModule, &sipModule_{module_name}) < 0)
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

/* The module's free slot. */
static void module_free(void *mod_ptr)
{
    sipModuleFree(mod_ptr);
}
''')

    @staticmethod
    def _g_module_function_table_entries(sf, bindings, module, members,
            has_module_functions=False):
        """ Generate the entries in a table of PyMethodDef for module
        functions.
        """

        for member in members:
            if member.py_slot is None:
                if not has_module_functions:
                    sf.write(f'\n\nstatic PyMethodDef sip_methods_{module.py_name}[] = {{\n')
                    has_module_functions = True

                py_name = member.py_name.name

                sf.write(f'    {{"{py_name}", SIP_MLMETH_CAST(func_{py_name}), METH_FASTCALL')

                if member.no_arg_parser or member.allow_keyword_args:
                    sf.write('|METH_KEYWORDS')

                docstring_ref = get_optional_ptr(
                        has_method_docstring(bindings, member,
                                module.overloads),
                        'doc_' + member.py_name.name)
                sf.write(f', {docstring_ref}}},\n')

        return has_module_functions

    @staticmethod
    def _g_module_traverse(sf):
        """ Generate the module traverse slot. """

        sf.write(
'''

/* The module's traverse slot. */
static int module_traverse(PyObject *mod, visitproc visit, void *arg)
{
    return sipModuleTraverse(mod, visit, arg);
}
''')

    def _g_slots_table(self, sf, type_name, members, mixin=False):
        """ Generate the slots table for a type.  Return the name of the table
        or None if nothing was generated.
        """

        slots = []
        has_setdelitem_slots = False
        has_rich_compare_slots = False

        if mixin:
            slots.append(('Py_tp_init', 'mixin_' + type_name))

        for member in members:
            if member.py_slot is None:
                continue

            if member.py_slot in (PySlot.SETITEM, PySlot.DELITEM):
                has_setdelitem_slots = True
                continue

            if is_rich_compare_slot(member.py_slot):
                has_rich_compare_slots = True
                continue

            if member.py_slot is PySlot.GETITEM:
                slots.append(('Py_sq_item', f'slot_{type_name}___sq_item__'))

            self._append_slot_table_entry(slots, type_name, member)

        if has_rich_compare_slots:
            slots.append(
                    ('Py_tp_richcompare',
                            f'slot_{type_name}___richcompare__'))

        if has_setdelitem_slots:
            slots.append(
                    ('Py_mp_ass_subscript',
                            f'slot_{type_name}___mp_ass_subscript__'))
            slots.append(
                    ('Py_sq_ass_item', f'slot_{type_name}___sq_ass_item__'))

        if slots:
            table_name = 'sipSlots_' + type_name

            sf.write(
f'''

/* Define this type's Python slots. */
static PyType_Slot {table_name}[] = {{
''')

            for slot_id, slot_impl in slots:
                sf.write(f'    {{{slot_id}, (void *){slot_impl}}},\n')

            sf.write('    {}\n};\n')
        else:
            table_name = None

        return table_name

    def _g_variables_table(self, sf, scope, *, for_unbound):
        """ Generate the table of either bound or unbound variables for a scope
        and return the length of the table.
        """

        spec = self.spec
        c_bindings = spec.c_bindings
        module = spec.module

        # Get the sorted list of variables.
        variables = list(variables_in_scope(spec, scope, check_handler=False))

        # Add the members of any anonymous enums.  Note that this would be
        # be better handled by the parser but that would require refactoring of
        # the legacy backend.
        for enum in spec.enums:
            if py_scope(enum.scope) is not scope:
                continue

            if enum.fq_cpp_name is not None:
                # Add the legacy support for members of custom enums to be
                # visible in the same scope as the enum.
                if not self.custom_enums_supported() or enum.is_scoped:
                    continue

            for member in enum.members:
                fq_cpp_name = ScopedName.parse(get_enum_member(spec, member))
                base_type = enum.enum_base_type or Argument(ArgumentType.INT)

                pseudo_var = WrappedVariable(fq_cpp_name, enum.module,
                        member.py_name, scope, base_type)

                # This is a bit of a hack.
                pseudo_var._enum_member = self._get_enum_member_value_field(
                        member, base_type=base_type)

                variables.append(pseudo_var)

        variables.sort(key=lambda k: k.py_name.name)

        table = []

        for variable in variables:
            # Check we are handling this sort of variable.
            if scope is None or variable.is_static:
                if not for_unbound:
                    continue
            elif for_unbound:
                continue

            v_type = variable.type
            v_ref = variable.fq_cpp_name.as_word

            # Generally const variables cannot be set.  However for string
            # pointers the reverse is true as a const pointer can be replaced
            # by another, but we can't allow a the contents of a non-const
            # string/array to be modified by C/C++ because they are immutable
            # in Python.
            not_settable = False
            might_need_key = False

            enum_member_value = getattr(variable, '_enum_member', None)

            # TODO Classes and mapped types.
            if v_type.type is ArgumentType.CLASS or (v_type.type is ArgumentType.ENUM and v_type.definition.fq_cpp_name is None):
                pass

            elif v_type.type is ArgumentType.ENUM and v_type.definition.fq_cpp_name is not None:
                type_id = self.get_type_ref(v_type.definition)
                not_settable = v_type.is_const

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
                # we don't have anywhere to store it.  (SIP_WV_RO is a special
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

            read_only = not_settable or variable.no_setter

            fields = []

            fields.append(f'.name = "{variable.py_name.name}"')
            fields.append('.type_id = ' + type_id)

            if enum_member_value is not None:
                fields.append('.key = SIP_WV_LITERAL')
                fields.append(enum_member_value)
            else:
                if read_only:
                    fields.append('.key = SIP_WV_RO')
                elif might_need_key:
                    fields.append('.key = ' + str(module.next_key))
                    module.next_key -= 1

                if scope is None or variable.is_static:
                    # TODO Why STRIP_GLOBAL here in particular?
                    cpp_name = variable.fq_cpp_name.cpp_stripped(STRIP_GLOBAL)
                    address = '&' + cpp_name
                else:
                    address = 'sipVariableAddrGetter_' + v_ref

                fields.append('.value.ptr_t = (void *)' + address)

            if variable.get_code is not None:
                fields.append('.get_code = sipVariableGetCode_' + v_ref)

            if variable.set_code is not None:
                fields.append('.set_code = sipVariableSetCode_' + v_ref)

            table.append(fields)

            # Generate any %GetCode wrapper.
            if variable.get_code is not None:
                # TODO Support sipPyType when scope is not None.
                # TODO Review the need to cache class instances (see legacy
                # variable handlers).  Or is that now in the sip module
                # wrapper?
                sf.write('\n')

                if not c_bindings:
                    sf.write(f'extern "C" {{static PyObject *sipVariableGetCode_{v_ref}();}}\n')

                sf.write(
f'''static PyObject *sipVariableGetCode_{v_ref}()
{{
    PyObject *sipPy;

''')

                sf.write_code(variable.get_code)

                sf.write(
'''
    return sipPy;
}

''')

            # Generate any %SetCode wrapper.
            if variable.set_code is not None:
                # TODO Support sipPyType when scope is not None.
                sf.write('\n')

                if not c_bindings:
                    sf.write(f'extern "C" {{static int sipVariableSetCode_{v_ref}(PyObject *);}}\n')

                sf.write(
f'''static int sipVariableSetCode_{v_ref}(PyObject *sipPy)
{{
    int sipErr = 0;

''')

                sf.write_code(variable.set_code)

                sf.write(
'''
    return sipErr ? -1 : 0;
}

''')

            # See if we need a descriptor address getter.
            if scope is None or variable.is_static or enum_member_value is not None:
                continue

            sf.write('\n\n')

            # TODO Why STRIP_GLOBAL here in particular?
            scope_name = scope.iface_file.fq_cpp_name.cpp_stripped(
                    STRIP_GLOBAL)
            cast = get_type_from_void(spec, scope_name, 'sipCppV')

            if not c_bindings:
                sf.write(f'extern "C" {{static void *sipVariableAddrGetter_{v_ref}(void *);}}\n')

            sf.write(
f'''static void *sipVariableAddrGetter_{v_ref}(void *sipCppV)
{{
    return &{cast}->{variable.py_name.name};
}}
''')

        nr_variables = len(table)

        if nr_variables != 0:
            if scope is None:
                scope_type = 'module'
                table_type = 'Module'
                suffix = module.py_name
            else:
                scope_type = 'type'
                table_type = 'Static' if for_unbound else 'Instance'
                suffix = scope.iface_file.fq_cpp_name.as_word

            sf.write(
f'''
/* Define the {table_type.lower()} variables for the {scope_type}. */
static const sipVariableSpec sip{table_type}Variables_{suffix}[] = {{
''')

            for fields in table:
                line = ', '.join(fields)
                sf.write(f'    {{{line}}},\n')

            if not for_unbound:
                sf.write('    {}\n')

            sf.write('};\n')

        return nr_variables

    @staticmethod
    def _append_slot_table_entry(slots, scope_name, member):
        """ Append an entry in the slot table for a scope. """

        # setitem, delitem and the rich comparison slots are handled elsewhere.

        py_slot = member.py_slot
        py_name = member.py_name

        slot_id = _SLOT_ID_MAP[py_slot]
        slots.append((slot_id, f'slot_{scope_name}_{py_name}'))

        # __len__ is placed in two slots.
        if py_slot is PySlot.LEN:
            slots.append(('Py_sq_length', f'slot_{scope_name}_{py_name}'))

    def _get_enum_member_value_field(self, member, base_type=None):
        """ Return the initialisation of the value field of an enum member
        specification.
        """

        if base_type is not None:
            arg_type = base_type.type
        elif member.scope.enum_base_type is not None:
            arg_type = member.scope.enum_base_type.type
        else:
            arg_type = ArgumentType.INT

        field, cast = _ENUM_MEMBER_TYPE_MAP[arg_type]

        return f'.value.{field} = static_cast<{cast}>({get_enum_member(self.spec, member)})'


# The mapping of an enum base type to the details needed to initialise a member
# specification.
_ENUM_MEMBER_TYPE_MAP = {
    ArgumentType.BOOL: ('bool_t', 'bool'),
    ArgumentType.BYTE: ('byte_t', 'char'),
    ArgumentType.STRING: ('byte_t', 'char'),
    ArgumentType.SBYTE: ('sbyte_t', 'signed char'),
    ArgumentType.SSTRING: ('sbyte_t', 'signed char'),
    ArgumentType.UBYTE: ('ubyte_t', 'unsigned char'),
    ArgumentType.USTRING: ('ubyte_t', 'unsigned char'),
    ArgumentType.SHORT: ('short_t', 'short'),
    ArgumentType.USHORT: ('ushort_t', 'unsigned short'),
    ArgumentType.INT: ('int_t', 'int'),
    ArgumentType.UINT: ('uint_t', 'unsigned'),
    ArgumentType.LONG: ('long_t', 'long'),
    ArgumentType.ULONG: ('ulong_t', 'unsigned long'),
    ArgumentType.LONGLONG: ('longlong_t', 'long long'),
    ArgumentType.ULONGLONG: ('ulonglong_t', 'unsigned long long'),
}


# The mapping of slots to Python slot IDs.
_SLOT_ID_MAP = {
    PySlot.STR: 'Py_tp_str',
    PySlot.INT: 'Py_nb_int',
    PySlot.FLOAT: 'Py_nb_float',
    PySlot.LEN: 'Py_mp_length',
    PySlot.CONTAINS: 'Py_sq_contains',
    PySlot.ADD: 'Py_nb_add',
    PySlot.CONCAT: 'Py_sq_concat',
    PySlot.SUB: 'Py_nb_subtract',
    PySlot.MUL: 'Py_nb_multiply',
    PySlot.REPEAT: 'Py_sq_repeat',
    PySlot.MOD: 'Py_nb_remainder',
    PySlot.FLOORDIV: 'Py_nb_floor_divide',
    PySlot.TRUEDIV: 'Py_nb_true_divide',
    PySlot.AND: 'Py_nb_and',
    PySlot.OR: 'Py_nb_or',
    PySlot.XOR: 'Py_nb_xor',
    PySlot.LSHIFT: 'Py_nb_lshift',
    PySlot.RSHIFT: 'Py_nb_rshift',
    PySlot.IADD: 'Py_nb_inplace_add',
    PySlot.ICONCAT: 'Py_sq_inplace_concat',
    PySlot.ISUB: 'Py_nb_inplace_subtract',
    PySlot.IMUL: 'Py_nb_inplace_multiply',
    PySlot.IREPEAT: 'Py_sq_inplace_repeat',
    PySlot.IMOD: 'Py_nb_inplace_remainder',
    PySlot.IFLOORDIV: 'Py_nb_inplace_floor_divide',
    PySlot.ITRUEDIV: 'Py_nb_inplace_true_divide',
    PySlot.IAND: 'Py_nb_inplace_and',
    PySlot.IOR: 'Py_nb_inplace_or',
    PySlot.IXOR: 'Py_nb_inplace_xor',
    PySlot.ILSHIFT: 'Py_nb_inplace_lshift',
    PySlot.IRSHIFT: 'Py_nb_inplace_rshift',
    PySlot.INVERT: 'Py_nb_invert',
    PySlot.CALL: 'Py_tp_call',
    PySlot.GETITEM: 'Py_mp_subscript',
    PySlot.BOOL: 'Py_nb_bool',
    PySlot.NEG: 'Py_nb_negative',
    PySlot.REPR: 'Py_tp_repr',
    PySlot.HASH: 'Py_tp_hash',
    PySlot.POS: 'Py_nb_positive',
    PySlot.ABS: 'Py_nb_absolute',
    PySlot.INDEX: 'Py_nb_index',
    PySlot.ITER: 'Py_tp_iter',
    PySlot.NEXT: 'Py_tp_iternext',
    PySlot.SETATTR: 'Py_tp_setattro',
    PySlot.MATMUL: 'Py_nb_matrix_multiply',
    PySlot.IMATMUL: 'Py_nb_inplace_matrix_multiply',
    PySlot.AWAIT: 'Py_am_await',
    PySlot.AITER: 'Py_am_aiter',
    PySlot.ANEXT: 'Py_am_anext',
}
