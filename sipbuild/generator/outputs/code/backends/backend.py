# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from .....sip_module_configuration import SipModuleConfiguration

from ....python_slots import is_number_slot
from ....scoped_name import STRIP_GLOBAL
from ....specification import (AccessSpecifier, ArgumentType, ArrayArgument,
        IfaceFileType, KwArgs, MappedType, PySlot, Transfer, WrappedClass,
        WrappedEnum)

from ...formatters import (fmt_argument_as_cpp_type, fmt_argument_as_name,
        fmt_class_as_scoped_name, fmt_class_as_scoped_py_name,
        fmt_scoped_py_name, fmt_signature_as_type_hint,
        fmt_value_list_as_cpp_expression)

from ..utils import (arg_is_small_enum, callable_overloads,
        get_convert_to_type_code, get_normalised_cached_name,
        has_method_docstring, is_used_in_code, need_error_flag, py_scope,
        release_gil, skip_overload)


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

    def g_arg_parser(self, sf, scope, py_signature, ctor=None, overload=None):
        """ Generate the argument variables for a member
        function/constructor/operator.
        """

        spec = self.spec

        # If the scope is just a namespace, then ignore it.
        if isinstance(scope, WrappedClass) and scope.iface_file.type is IfaceFileType.NAMESPACE:
            scope = None

        # For ABI v13 and later static methods use self for the type object.
        if spec.target_abi >= (13, 0):
            handle_self = (scope is not None and overload is not None and overload.common.py_slot is None)
        else:
            handle_self = (scope is not None and overload is not None and overload.common.py_slot is None and not overload.is_static)

        # Generate the local variables that will hold the parsed arguments and
        # values returned via arguments.
        array_len_arg_nr = -1
        need_owner = False
        ctor_needs_self = False

        for arg_nr, arg in enumerate(py_signature.args):
            if arg.array is ArrayArgument.ARRAY_SIZE:
                array_len_arg_nr = arg_nr

            self._g_argument_variable(sf, scope, arg, arg_nr)

            if arg.transfer is Transfer.TRANSFER_THIS:
                need_owner = True

            if ctor is not None and arg.transfer is Transfer.TRANSFER:
                ctor_needs_self = True

        if overload is not None and need_owner:
            sf.write('        sipWrapper *sipOwner = SIP_NULLPTR;\n')

        if handle_self and not overload.is_static:
            cpp_type = 'const ' if overload.is_const else ''

            if overload.access_specifier is AccessSpecifier.PROTECTED and scope.has_shadow:
                cpp_type += 'sip' + scope.iface_file.fq_cpp_name.as_word
            else:
                cpp_type += self.scoped_class_name(scope)

            sf.write(f'        {cpp_type} *sipCpp;\n\n')
        elif len(py_signature.args) != 0:
            sf.write('\n')

        # Generate the call to the parser function.
        args = []
        single_arg = False

        if spec.target_abi >= (14, 0):
            args.append('sipModule')

        if overload is not None and is_number_slot(overload.common.py_slot):
            parser_function = 'sipParsePair'
            args.append('&sipParseErr')
            args.append('sipArg0')
            args.append('sipArg1')

        elif overload is not None and overload.common.py_slot is PySlot.SETATTR:
            # We don't even try to invoke the parser if there is a value and
            # there shouldn't be (or vice versa) so that the list of errors
            # doesn't get polluted with signatures that can never apply.
            if overload.is_delattr:
                operator = '=='
                sip_value = 'SIP_NULLPTR'
            else:
                operator = '!='
                sip_value = 'sipValue'

            parser_function = f'sipValue {operator} SIP_NULLPTR && sipParsePair'
            args.append('&sipParseErr')
            args.append('sipName')
            args.append(sip_value)

        elif (overload is not None and overload.common.allow_keyword_args) or ctor is not None:
            # We handle keywords if we might have been passed some (because one
            # of the overloads uses them or we are a ctor).  However this
            # particular overload might not have any.
            if overload is not None:
                kw_args = overload.kw_args
            elif ctor is not None:
                kw_args = ctor.kw_args
            else:
                kw_args = KwArgs.NONE

            # The above test isn't good enough because when the flags were set
            # in the parser we couldn't know for sure if an argument was an
            # output pointer.  Therefore we check here.  The drawback is that
            # we may generate the name string for the argument but never use
            # it, or we might have an empty keyword name array or one that
            # contains only NULLs.
            is_ka_list = False

            if kw_args is not KwArgs.NONE:
                for arg in py_signature.args:
                    if not arg.is_in:
                        continue

                    if not is_ka_list:
                        sf.write('        static const char *sipKwdList[] = {\n')
                        is_ka_list = True

                    if arg.name is not None and (kw_args is KwArgs.ALL or arg.default_value is not None):
                        arg_name_ref = self.cached_name_ref(arg.name)
                    else:
                        arg_name_ref = 'SIP_NULLPTR'

                    sf.write(f'            {arg_name_ref},\n')

                if is_ka_list:
                    sf.write('        };\n\n')

            parser_function = 'sipParseKwdArgs'
            args.append('sipParseErr' if ctor is not None else '&sipParseErr')
            args.append('sipArgs')

            if spec.target_abi >= (14, 0):
                args.append('sipNrArgs')

            args.append('sipKwds')
            args.append('sipKwdList' if is_ka_list else 'SIP_NULLPTR')
            args.append('sipUnused' if ctor is not None else 'SIP_NULLPTR')

        else:
            # ABI v14 doesn't require the single-argument optimisation.
            if spec.target_abi >= (14, 0):
                single_arg = False
            else:
                single_arg = not (overload is None or overload.common.py_slot is None or is_multi_arg_slot(overload.common.py_slot))

            plural = '' if single_arg else 's'

            parser_function = 'sipParseArgs'
            args.append('&sipParseErr')
            args.append('sipArg' + plural)

            if spec.target_abi >= (14, 0):
                args.append('sipNrArgs')

        # Generate the format string.
        format_s = '"'
        optional_args = False

        if single_arg:
            format_s += '1'

        if ctor_needs_self:
            format_s += '#'
        elif handle_self:
            if overload.is_static:
                format_s += 'C'
            elif overload.access_is_really_protected:
                format_s += 'p'
            else:
                format_s += 'B'

        for arg in py_signature.args:
            if not arg.is_in:
                continue

            if arg.default_value is not None and not optional_args:
                format_s += '|'
                optional_args = True

            # Get the wrapper if explicitly asked for or we are going to keep a
            # reference to.  However if it is an encoded string then we will
            # get the actual wrapper from the format character.
            if arg.get_wrapper:
                format_s += '@'
            elif arg.key is not None:
                if not (arg.type in (ArgumentType.ASCII_STRING, ArgumentType.LATIN1_STRING, ArgumentType.UTF8_STRING) and len(arg.derefs) == 1):
                    format_s += '@'

            if arg.type is ArgumentType.ASCII_STRING:
                format_s += 'AA' if _is_string(arg) else 'aA'

            elif arg.type is ArgumentType.LATIN1_STRING:
                format_s += 'AL' if _is_string(arg) else 'aL'

            elif arg.type is ArgumentType.UTF8_STRING:
                format_s += 'A8' if _is_string(arg) else 'a8'

            elif arg.type in (ArgumentType.SSTRING, ArgumentType.USTRING, ArgumentType.STRING):
                if arg.array is ArrayArgument.ARRAY:
                    format_s += 'k'
                elif _is_string(arg):
                    format_s += 's'
                else:
                    format_s += 'c'

            elif arg.type is ArgumentType.WSTRING:
                if arg.array is ArrayArgument.ARRAY:
                    format_s += 'K'
                elif _is_string(arg):
                    format_s += 'x'
                else:
                    format_s += 'w'

            elif arg.type is ArgumentType.ENUM:
                if arg.definition.fq_cpp_name is None:
                    format_s += 'e'
                elif arg.is_constrained:
                    format_s += 'XE'
                else:
                    format_s += 'E'
            elif arg.type is ArgumentType.BOOL:
                format_s += 'b'

            elif arg.type is ArgumentType.CBOOL:
                format_s += 'Xb'

            elif arg.type is ArgumentType.INT:
                if arg.array is not ArrayArgument.ARRAY_SIZE:
                    format_s += 'i'

            elif arg.type is ArgumentType.UINT:
                if arg.array is not ArrayArgument.ARRAY_SIZE:
                    format_s += 'u'

            elif arg.type is ArgumentType.SIZE:
                if arg.array is not ArrayArgument.ARRAY_SIZE:
                    format_s += '='

            elif arg.type is ArgumentType.CINT:
                format_s += 'Xi'

            elif arg.type is ArgumentType.BYTE:
                if arg.array is not ArrayArgument.ARRAY_SIZE:
                    format_s += 'I' if self.abi_has_working_char_conversion() else 'L'

            elif arg.type is ArgumentType.SBYTE:
                if arg.array is not ArrayArgument.ARRAY_SIZE:
                    format_s += 'L'

            elif arg.type is ArgumentType.UBYTE:
                if arg.array is not ArrayArgument.ARRAY_SIZE:
                    format_s += 'M'

            elif arg.type is ArgumentType.SHORT:
                if arg.array is not ArrayArgument.ARRAY_SIZE:
                    format_s += 'h'

            elif arg.type is ArgumentType.USHORT:
                if arg.array is not ArrayArgument.ARRAY_SIZE:
                    format_s += 't'

            elif arg.type is ArgumentType.LONG:
                if arg.array is not ArrayArgument.ARRAY_SIZE:
                    format_s += 'l'

            elif arg.type is ArgumentType.ULONG:
                if arg.array is not ArrayArgument.ARRAY_SIZE:
                    format_s += 'm'

            elif arg.type is ArgumentType.LONGLONG:
                if arg.array is not ArrayArgument.ARRAY_SIZE:
                    format_s += 'n'

            elif arg.type is ArgumentType.ULONGLONG:
                if arg.array is not ArrayArgument.ARRAY_SIZE:
                    format_s += 'o'

            elif arg.type in (ArgumentType.STRUCT, ArgumentType.UNION, ArgumentType.VOID):
                format_s += 'v'

            elif arg.type is ArgumentType.CAPSULE:
                format_s += 'z'

            elif arg.type is ArgumentType.FLOAT:
                format_s += 'f'

            elif arg.type is ArgumentType.CFLOAT:
                format_s += 'Xf'

            elif arg.type is ArgumentType.DOUBLE:
                format_s += 'd'

            elif arg.type is ArgumentType.CDOUBLE:
                format_s += 'Xd'

            elif arg.type in (ArgumentType.CLASS, ArgumentType.MAPPED):
                if arg.array is ArrayArgument.ARRAY:
                    format_s += '>' if arg.type is ArgumentType.CLASS and self.abi_supports_array() else 'r'
                else:
                    format_s += 'J' + _get_subformat_char(arg)

            elif arg.type is ArgumentType.PYOBJECT:
                format_s += 'P' + _get_subformat_char(arg)

            elif arg.type in (ArgumentType.PYTUPLE, ArgumentType.PYLIST, ArgumentType.PYDICT, ArgumentType.PYSLICE, ArgumentType.PYTYPE):
                format_s += 'N' if arg.allow_none else 'T'

            elif arg.type is ArgumentType.PYCALLABLE:
                format_s += 'H' if arg.allow_none else 'F'

            elif arg.type is ArgumentType.PYBUFFER:
                format_s += '$' if arg.allow_none else '!'

            elif arg.type is ArgumentType.PYENUM:
                format_s += '^' if arg.allow_none else '&'

            elif arg.type is ArgumentType.ELLIPSIS:
                format_s += 'W'

        format_s += '"'
        args.append(format_s)

        # Generate the parameters corresponding to the format string.
        if ctor_needs_self:
            args.append('sipSelf')
        elif handle_self:
            args.append('&sipSelf')

            if not overload.is_static:
                args.append(self.get_type_ref(scope))
                args.append('&sipCpp')

        for arg_nr, arg in enumerate(py_signature.args):
            if not arg.is_in:
                continue

            arg_name = fmt_argument_as_name(spec, arg, arg_nr)
            arg_name_ref = '&' + arg_name

            # Use the wrapper name if it was explicitly asked for.
            if arg.get_wrapper:
                args.append(f'&{arg_name}Wrapper')
            elif arg.key is not None:
                args.append(f'&{arg_name}Keep')

            if arg.type is ArgumentType.MAPPED:
                mapped_type = arg.definition

                args.append(self.get_type_ref(mapped_type))
                args.append(arg_name_ref)

                if arg.array is ArrayArgument.ARRAY:
                    array_len_arg_name = fmt_argument_as_name(spec,
                            py_signature.args[array_len_arg_nr],
                            array_len_arg_nr)
                    args.append('&' + array_len_arg_name)
                elif mapped_type.convert_to_type_code is not None and not arg.is_constrained:
                    args.append('SIP_NULLPTR' if mapped_type.no_release else f'&{arg_name}State')

                    if mapped_type.needs_user_state:
                        args.append(f'&{arg_name}UserState')

            elif arg.type is ArgumentType.CLASS:
                klass = arg.definition

                args.append(self.get_type_ref(klass))
                args.append(arg_name_ref)

                if arg.array is ArrayArgument.ARRAY:
                    array_len_arg_name = fmt_argument_as_name(spec,
                            py_signature.args[array_len_arg_nr],
                            array_len_arg_nr)
                    args.append('&' + array_len_arg_name)

                    if self.abi_supports_array():
                        args.append(f'&{arg_name}IsTemp')
                else:
                    if arg.transfer is Transfer.TRANSFER_THIS:
                        args.append('sipOwner' if ctor is not None else '&sipOwner')

                    if klass.convert_to_type_code is not None and not arg.is_constrained:
                        args.append(f'&{arg_name}State')

            elif arg.type in (ArgumentType.ASCII_STRING, ArgumentType.LATIN1_STRING, ArgumentType.UTF8_STRING):
                if arg.key is None and len(arg.derefs) == 1:
                    args.append(f'&{arg_name}Keep')

                args.append(arg_name_ref)

            elif arg.type is ArgumentType.PYTUPLE:
                args.append('&PyTuple_Type')
                args.append(arg_name_ref)

            elif arg.type is ArgumentType.PYLIST:
                args.append('&PyList_Type')
                args.append(arg_name_ref)

            elif arg.type is ArgumentType.PYDICT:
                args.append('&PyDict_Type')
                args.append(arg_name_ref)

            elif arg.type is ArgumentType.PYSLICE:
                args.append('&PySlice_Type')
                args.append(arg_name_ref)

            elif arg.type is ArgumentType.PYTYPE:
                args.append('&PyType_Type')
                args.append(arg_name_ref)

            elif arg.type is ArgumentType.ENUM:
                if arg.definition.fq_cpp_name is not None:
                    args.append(self.get_type_ref(arg.definition))

                args.append(arg_name_ref)

            elif arg.type is ArgumentType.CAPSULE:
                args.append('"' + arg.definition.as_cpp + '"')
                args.append(arg_name_ref)

            else:
                if arg.array is not ArrayArgument.ARRAY_SIZE:
                    args.append(arg_name_ref)

                if arg.array is ArrayArgument.ARRAY:
                    array_len_arg_name = fmt_argument_as_name(spec,
                            py_signature.args[array_len_arg_nr],
                            array_len_arg_nr)
                    args.append('&' + array_len_arg_name)

        args = ', '.join(args)

        sf.write(f'        if ({parser_function}({args}))\n')

    def g_call_args(self, sf, cpp_signature, py_signature):
        """ Generate typed arguments for a call. """

        spec = self.spec

        for arg_nr, arg in enumerate(cpp_signature.args):
            if arg_nr > 0:
                sf.write(', ')

            # See if the argument needs dereferencing or it's address taking.
            indirection = ''
            nr_derefs = len(arg.derefs)

            # The argument may be surrounded by something type-specific.
            prefix = suffix = ''

            if arg.type in (ArgumentType.ASCII_STRING, ArgumentType.LATIN1_STRING, ArgumentType.UTF8_STRING, ArgumentType.SSTRING, ArgumentType.USTRING, ArgumentType.STRING, ArgumentType.WSTRING):
                if nr_derefs > (0 if arg.is_out else 1) and not arg.is_reference:
                    indirection = '&'

            elif arg.type in (ArgumentType.CLASS, ArgumentType.MAPPED):
                if nr_derefs == 2:
                    indirection = '&'
                elif nr_derefs == 0:
                    indirection = '*'

                    if arg.type is ArgumentType.MAPPED and arg.definition.movable:
                        prefix = 'std::move('
                        suffix = ')'

            elif arg.type in (ArgumentType.STRUCT, ArgumentType.UNION, ArgumentType.VOID):
                if nr_derefs == 2:
                    indirection = '&'

            else:
                if nr_derefs == 1:
                    indirection = '&'

                if arg_is_small_enum(arg):
                    prefix = 'static_cast<' + fmt_enum_as_cpp_type(arg.definition) + '>('
                    suffix = ')'

            # See if we need to cast a Python void * to the correct C/C++
            # pointer type.  Note that we assume that the arguments correspond
            # and are just different types.
            need_cast = False

            if py_signature is not cpp_signature and len(py_signature.args) == len(cpp_signature.args):
                py_arg = py_signature.args[arg_nr]

                VOID_TYPES = (ArgumentType.VOID, ArgumentType.CAPSULE)

                if py_arg.type in VOID_TYPES and arg.type not in VOID_TYPES and len(py_arg.derefs) == nr_derefs:
                    need_cast = True

            arg_name = fmt_argument_as_name(spec, arg, arg_nr)
            arg_cpp_type_name = fmt_argument_as_cpp_type(spec, arg, plain=True,
                    no_derefs=True)

            if need_cast:
                if spec.c_bindings:
                    sf.write(f'({arg_cpp_type_name} *){arg_name}')
                else:
                    sf.write(f'reinterpret_cast<{arg_cpp_type_name} *>({arg_name})')
            else:
                sf.write(prefix + indirection)

                if arg.array is ArrayArgument.ARRAY_SIZE:
                    sf.write(f'({arg_cpp_type_name})')

                sf.write(arg_name + suffix)

    def g_class_docstring(self, sf, bindings, klass):
        """ Generate any docstring for a class and return an appropriate
        reference to it.
        """

        if _has_class_docstring(bindings, klass):
            docstring_ref = 'doc_' + klass.iface_file.fq_cpp_name.as_word

            sf.write(f'\nPyDoc_STRVAR({docstring_ref}, "')
            self._g_class_docstring(sf, bindings, klass)
            sf.write('");\n')
        else:
            docstring_ref = 'SIP_NULLPTR'

        return docstring_ref

    def g_catch(self, sf, bindings, py_signature, throw_args, release_gil):
        """ Generate the catch blocks for a call. """

        if not _handling_exceptions(bindings, throw_args):
            return

        spec = self.spec

        use_handler = self.abi_has_next_exception_handler()

        sf.write('            }\n')

        if not use_handler:
            if throw_args is not None:
                for exception in throw_args.arguments:
                    self.g_catch_block(sf, exception,
                            py_signature=py_signature, release_gil=release_gil)
            elif spec.module.default_exception is not None:
                self.g_catch_block(sf, spec.module.default_exception,
                        py_signature=py_signature, release_gil=release_gil)

        sf.write(
'''            catch (...)
            {
''')

        if release_gil:
            sf.write(
'''                Py_BLOCK_THREADS

''')

        self._g_delete_outs(sf, py_signature)
        self.g_delete_temporaries(sf, py_signature)

        if use_handler:
            sf.write(
'''                void *sipExcState = SIP_NULLPTR;
                sipExceptionHandler sipExcHandler;
                std::exception_ptr sipExcPtr = std::current_exception();

                while ((sipExcHandler = sipNextExceptionHandler(&sipExcState)) != SIP_NULLPTR)
                    if (sipExcHandler(sipExcPtr))
                        return SIP_NULLPTR;

''')

        sf.write(
'''                sipRaiseUnknownException();
                return SIP_NULLPTR;
            }
''')

    def g_catch_block(self, sf, exception, py_signature=None,
            release_gil=False):
        """ Generate a single catch block. """

        exception_fq_cpp_name = exception.iface_file.fq_cpp_name

        # The global scope is stripped from the exception name to be consistent
        # with older versions of SIP.
        exception_cpp_stripped = exception_fq_cpp_name.cpp_stripped(
                STRIP_GLOBAL)

        sip_exception_ref = 'sipExceptionRef' if exception.class_exception is not None or is_used_in_code(exception.raise_code, 'sipExceptionRef') else ''

        sf.write(
f'''            catch ({exception_cpp_stripped} &{sip_exception_ref})
            {{
''')

        if release_gil:
            sf.write(
'''
                Py_BLOCK_THREADS
''')

        if py_signature is not None:
            self._g_delete_outs(sf, py_signature)
            self.g_delete_temporaries(sf, py_signature)
            result = 'SIP_NULLPTR'
        else:
            result = 'true'

        # See if the exception is a wrapped class.
        if exception.class_exception is not None:
            exception_cpp = exception_fq_cpp_name.as_cpp

            sf.write(
f'''                /* Hope that there is a valid copy ctor. */
                {exception_cpp} *sipExceptionCopy = new {exception_cpp}(sipExceptionRef);

                sipRaiseTypeException({self.get_type_ref(exception)}, sipExceptionCopy);
''')
        else:
            sf.write_code(exception.raise_code)

        sf.write(
f'''
                return {result};
            }}
''')

    def g_class_api(self, sf, klass):
        """ Generate the C++ API for a class. """

        spec = self.spec
        iface_file = klass.iface_file

        module_name = spec.module.py_name

        sf.write('\n')

        if klass.real_class is None and not klass.is_hidden_namespace:
            sf.write(f'#define {self.get_type_ref(klass)} {self.get_class_ref_value(klass)}\n')

        self.g_enum_macros(sf, scope=klass)

        if not klass.external and not klass.is_hidden_namespace:
            klass_name = iface_file.fq_cpp_name.as_word
            sf.write(f'\nextern sipClassTypeDef sipTypeDef_{module_name}_{klass_name};\n')

    def g_class_method_table(self, sf, bindings, klass):
        """ Generate the sorted table of methods for a class and return the
        number of entries.
        """

        if klass.iface_file.type is IfaceFileType.NAMESPACE:
            members = _get_function_table(klass.members)
        else:
            members = _get_method_table(klass)

        return self._g_py_method_table(sf, bindings, members, klass)

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

        nr_static_variables, nr_types = static_variables_state

        sf.write(
f'''/* The wrapped module's immutable definition. */
static const sipWrappedModuleDef sipWrappedModule_{module_name} = {{
    .abi_major = {target_abi[0]},
    .abi_minor = {target_abi[1]},
    .sip_configuration = 0x{spec.sip_module_configuration:04x},
''')

        if len(module.all_imports) != 0:
            sf.write('    .imports = importsTable,\n')

        # TODO Exclude non-local types.  They are needed (ie. I think we need
        # the iface file) but we don't generated definition structures.
        if len(module.needed_types) != 0:
            sf.write(f'    .nr_type_defs = {len(module.needed_types)},\n')
            sf.write(f'    .type_defs = sipTypeDefs_{module_name},\n')

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

        if nr_static_variables != 0:
            sf.write(f'    .attributes.nr_static_variables = {nr_static_variables},\n')
            sf.write(f'    .attributes.static_variables = sipWrappedModuleVariables_{module_name},\n')

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

        self.g_module_docstring(sf)
        self.g_pyqt_helper_defns(sf)
        self._g_module_clear(sf)
        self._g_module_exec(sf)
        self._g_module_free(sf)
        self._g_module_traverse(sf)
        has_module_functions = self.g_module_functions_table(sf, bindings)
        self.g_module_definition(sf, has_module_functions=has_module_functions)
        self.g_module_init_start(sf)

        sf.write(
f'''    return PyModuleDef_Init(&sipWrappedModuleDef_{module_name});
}}
''')

    def g_delete_temporaries(self, sf, py_signature):
        """ Generate the code to delete any temporary variables on the heap
        created by type convertors.
        """

        spec = self.spec

        for arg_nr, arg in enumerate(py_signature.args):
            arg_name = fmt_argument_as_name(spec, arg, arg_nr)

            if arg.array is ArrayArgument.ARRAY and arg.type in (ArgumentType.CLASS, ArgumentType.MAPPED):
                if arg.transfer is not Transfer.TRANSFER:
                    extra_indent = ''

                    if arg.type is ArgumentType.CLASS and self.abi_supports_array():
                        sf.write(f'            if ({arg_name}IsTemp)\n')
                        extra_indent = '    '

                    if spec.c_bindings:
                        sf.write(f'            {extra_indent}sipFree({arg_name});\n')
                    else:
                        sf.write(f'            {extra_indent}delete[] {arg_name};\n')

                continue

            if not arg.is_in:
                continue

            if arg.type in (ArgumentType.ASCII_STRING, ArgumentType.LATIN1_STRING, ArgumentType.UTF8_STRING) and len(arg.derefs) == 1:
                decref = 'Py_XDECREF' if arg.default_value is not None else 'Py_DECREF'

                sf.write(f'            {decref}({arg_name}Keep);\n')

            elif arg.type is ArgumentType.WSTRING and len(arg.derefs) == 1:
                if spec.c_bindings or not arg.is_const:
                    sf.write(f'            sipFree({arg_name});\n')
                else:
                    sf.write(f'            sipFree(const_cast<wchar_t *>({arg_name}));\n')

            else:
                convert_to_type_code = get_convert_to_type_code(arg)

                if convert_to_type_code is not None:
                    if arg.type is ArgumentType.MAPPED and arg.definition.no_release:
                        continue

                    sf.write(f'            sipReleaseType{get_user_state_suffix(spec, arg)}(')

                    if spec.c_bindings or not arg.is_const:
                        sf.write(arg_name)
                    else:
                        arg_cpp_plain = fmt_argument_as_cpp_type(spec, arg,
                                plain=True, no_derefs=True)
                        sf.write(f'const_cast<{arg_cpp_plain} *>({arg_name})')

                    sf.write(f', {self.get_type_ref(arg.definition)}, {arg_name}State')

                    if type_needs_user_state(arg):
                        sf.write(f', {arg_name}UserState')

                    sf.write(');\n')

    def g_enum_macros(self, sf, scope=None, imported_module=None):
        """ Generate the type macros for enums. """

        # TODO
        pass

    def g_enum_member_table(self, sf, scope=None):
        """ Generate the table of enum members for a scope.  Return the number
        of them.
        """

        spec = self.spec
        enum_members = []

        for enum in spec.enums:
            if enum.module is not spec.module:
                continue

            enum_py_scope = py_scope(enum.scope)

            if isinstance(scope, WrappedClass):
                # The scope is a class.
                if enum_py_scope is not scope or (enum.is_protected and not scope.has_shadow):
                    continue

            elif scope is not None:
                # The scope is a mapped type.
                if enum.scope != scope:
                    continue

            elif enum_py_scope is not None or isinstance(enum.scope, MappedType) or enum.fq_cpp_name is None:
                continue

            enum_members.extend(enum.members)

        nr_members = len(enum_members)
        if nr_members == 0:
            return 0

        enum_members.sort(key=lambda v: v.scope.type_nr)
        enum_members.sort(key=lambda v: v.py_name.name)

        if py_scope(scope) is None:
            sf.write(
'''
/* These are the enum members of all global enums. */
static sipEnumMemberDef enummembers[] = {
''')
        else:
            sf.write(
f'''
static sipEnumMemberDef enummembers_{scope.iface_file.fq_cpp_name.as_word}[] = {{
''')

        for enum_member in enum_members:
            sf.write(f'    {{{self.cached_name_ref(enum_member.py_name)}, ')
            sf.write(self.get_enum_member(enum_member))
            sf.write(f', {enum_member.scope.type_nr}}},\n')

        sf.write('};\n')

        return nr_members

    def g_function_support_vars(self, sf):
        """ Generate the variables needed by a function. """

        sf.write('    const sipAPIDef *sipAPI = sipGetAPI(sipModule);\n')

    @staticmethod
    def g_gc_ellipsis(sf, signature):
        """ Generate the code to garbage collect any ellipsis argument. """

        last = len(signature.args) - 1

        if last >= 0 and signature.args[last].type is ArgumentType.ELLIPSIS:
            sf.write(f'\n            Py_DECREF(a{last});\n')

    def g_mapped_type_api(self, sf, mapped_type):
        """ Generate the API details for a mapped type. """

        # TODO
        pass

    def g_mapped_type_method_table(self, sf, bindings, mapped_type):
        """ Generate the sorted table of static methods for a mapped type and
        return the number of entries.
        """

        members = _get_function_table(mapped_type.members)

        return self._g_py_method_table(sf, bindings, members, mapped_type)

    def g_method_docstring(self, sf, bindings, member, overloads,
            is_method=False):
        """ Generate the docstring for all overloads of a function/method.
        Return True if the docstring was entirely automatically generated.
        """

        NEWLINE = '\\n"\n"'

        auto_docstring = True

        # See if all the docstrings are automatically generated.
        all_auto = True
        any_implied = False

        for overload in callable_overloads(member, overloads):
            if overload.docstring is not None:
                all_auto = False

                if overload.docstring.signature is not DocstringSignature.DISCARDED:
                    any_implied = True

        # Generate the docstring.
        is_first = True

        for overload in callable_overloads(member, overloads):
            if not is_first:
                sf.write(NEWLINE)

                # Insert a blank line if any explicit docstring wants to
                # include a signature.  This maintains compatibility with
                # previous versions.
                if any_implied:
                    sf.write(NEWLINE)

            if overload.docstring is not None:
                if overload.docstring.signature is DocstringSignature.PREPENDED:
                    self._g_method_auto_docstring(sf, bindings, overload,
                            is_method)
                    sf.write(NEWLINE)

                sf.write(self.docstring_text(overload.docstring))

                if overload.docstring.signature is DocstringSignature.APPENDED:
                    sf.write(NEWLINE)
                    self._g_method_auto_docstring(sf, bindings, overload,
                            is_method)

                auto_docstring = False
            elif all_auto or any_implied:
                self._g_method_auto_docstring(sf, bindings, overload,
                        is_method)

            is_first = False

        return auto_docstring

    def g_method_support_vars(self, sf):
        """ Generate the variables needed by a method. """

        # TODO
        pass

    def g_module_definition(self, sf, has_module_functions=False):
        """ Generate the module definition structure. """

        module = self.spec.module

        # TODO This value should be taken from a new option of the %Module
        # directive and default to Py_MOD_MULTIPLE_INTERPRETERS_NOT_SUPPORTED.
        interp_support = 'Py_MOD_MULTIPLE_INTERPRETERS_NOT_SUPPORTED'

        sf.write(
f'''

/* The wrapped module's immutable slot definitions. */
static PyModuleDef_Slot sip_wrapped_module_slots[] = {{
    {{Py_mod_exec, (void *)wrapped_module_exec}},
#if PY_VERSION_HEX >= 0x030c0000
    {{Py_mod_multiple_interpreters, {interp_support}}},
#endif
#if PY_VERSION_HEX >= 0x030d0000
    {{Py_mod_gil, Py_MOD_GIL_USED}},
#endif
    {{0, SIP_NULLPTR}}
}};


/* The wrapped module's immutable definition. */
PyModuleDef sipWrappedModuleDef_{module.py_name} = {{
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
            sf.write(f'    .m_doc = doc_mod_{module.py_name},\n')

        if has_module_functions:
            sf.write('    .m_methods = sip_methods,\n')

        sf.write('};\n')

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

    def g_pyqt_class_plugin(self, sf, bindings, klass):
        """ Generate any extended class definition data for PyQt.  Return True
        if anything was generated.
        """

        spec = self.spec

        is_signals = self._g_pyqt_signals_table(sf, bindings, klass)

        # The PyQt6 support code doesn't assume the structure is generated.
        if self.pyqt6_supported():
            generated = is_signals

            if klass.is_qobject and not klass.pyqt_no_qmetaobject:
                generated = True

            if klass.pyqt_interface is not None:
                generated = True

            if not generated:
                return False

        klass_name = klass.iface_file.fq_cpp_name.as_word

        pyqt_version = '5' if self.pyqt5_supported() else '6'
        sf.write(f'\n\nstatic pyqt{pyqt_version}ClassPluginDef plugin_{klass_name} = {{\n')

        mo_ref = f'&{self.scoped_class_name(klass)}::staticMetaObject' if klass.is_qobject and not klass.pyqt_no_qmetaobject else 'SIP_NULLPTR'
        sf.write(f'    {mo_ref},\n')

        if self.pyqt5_supported():
            sf.write(f'    {klass.pyqt_flags},\n')

        signals_ref = f'signals_{klass_name}' if is_signals else 'SIP_NULLPTR'
        sf.write(f'    {signals_ref},\n')

        interface_ref = f'"{klass.pyqt_interface}"' if klass.pyqt_interface is not None else 'SIP_NULLPTR'
        sf.write(f'    {interface_ref}\n')

        sf.write('};\n')

        return True

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
        sf.write(
f'''

extern PyModuleDef sipWrappedModuleDef_{module_name};

#define sipBuildResult              sipAPI->api_build_result
#define sipFindTypeID               sipAPI->api_find_type_id
#define sipGetAddress               sipAPI->api_get_address
#define sipIsOwnedByPython          sipAPI->api_is_owned_by_python
''')

        # TODO These have been reviewed as part of the private v14 API.
        sf.write(
f'''#define sipNoFunction               sipAPI->api_no_function
#define sipNoMethod                 sipAPI->api_no_method
#define sipParseArgs                sipAPI->api_parse_args
#define sipParseKwdArgs             sipAPI->api_parse_kwd_args
#define sipParsePair                sipAPI->api_parse_pair
''')

        # TODO These have yet to be reviewed.
        sf.write(
f'''#define sipMalloc                   sipAPI->api_malloc
#define sipFree                     sipAPI->api_free
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

    def g_slot_support_vars(self, sf):
        """ Generate the variables needed by a slot function. """

        sf.write(
f'''    PyObject *sipModule = sipGetModule(sipSelf, &sipWrappedModuleDef_{self.spec.module.py_name});
    const sipAPIDef *sipAPI = sipGetAPI(sipModule);

''')

    def g_static_variables_table(self, sf, scope=None):
        """ Generate the tables of static variables and types for a scope and
        return a 2-tuple of the length of each table.
        """

        # Do the variables.
        nr_variables = self._g_variables_table(sf, scope, for_static=True)

        # Do the types.
        # TODO Check this excludes non-local types.
        module = self.spec.module
        suffix = module.py_name if scope is None else scope.iface_file.fq_cpp_name.as_word

        nr_types = 0

        for type_nr, needed_type in enumerate(module.needed_types):
            if needed_type.type is ArgumentType.CLASS:
                klass = needed_type.definition

                if klass.scope is not scope or klass.external or klass.is_hidden_namespace:
                    continue

                if nr_types == 0:
                    sf.write(f'\nstatic const sipTypeNr sipTypeNrs_{suffix}[] = {{')
                else:
                    sf.write(', ')

                sf.write(str(type_nr))
                nr_types += 1

        if nr_types != 0:
            sf.write('};\n')

        return nr_variables, nr_types

    @staticmethod
    def g_try(sf, bindings, throw_args):
        """ Generate the try block for a call. """

        if not _handling_exceptions(bindings, throw_args):
            return

        sf.write(
'''            try
            {
''')

    def g_type_definition(self, sf, bindings, klass, py_debug):
        """ Generate the type structure that contains all the information
        needed by the meta-type.  A sub-set of this is used to extend
        namespaces.
        """

        spec = self.spec
        module = spec.module
        module_name = module.py_name
        klass_name = klass.iface_file.fq_cpp_name.as_word

        # Generate the static variables table.
        nr_static_variables, nr_types = self.g_static_variables_table(sf,
                scope=klass)

        # Generate the table of slots.
        # TODO
        has_slots = False

        if has_slots:
            sf.write(
f'''

static PyType_Slot sip_py_slots_{klass_name}[] = {{
    {{0, NULL}}
}};
''')

        # Generate the docstring.
        docstring_ref = self.g_class_docstring(sf, bindings, klass)

        # Generate the type definition itself.
        fields = []

        fields.append(
                '.ctd_base.td_flags = ' + self.get_class_flags(klass, py_debug))
        fields.append(
                '.ctd_base.td_cname = ' + self.cached_name_ref(klass.iface_file.cpp_name))

        if self.pyqt5_supported() or self.pyqt6_supported():
            if self.g_pyqt_class_plugin(sf, bindings, klass):
                fields.append(
                        '.ctd_base.td_plugin_data = &plugin_' + klass_name)

        if klass.real_class is None:
            fields.append(
                    f'.ctd_container.cod_name = "{fmt_class_as_scoped_py_name(klass)}"')

        if klass.real_class is not None:
            cod_scope = self.get_type_ref(klass.real_class)
        elif py_scope(klass.scope) is not None:
            cod_scope = self.get_type_ref(klass.scope)
        else:
            cod_scope = None

        if cod_scope is not None:
            fields.append('.ctd_container.cod_scope = ' + cod_scope)

        if nr_static_variables != 0:
            fields.append(
                    '.ctd_container.cod_attributes.nr_static_variables = ' + str(nr_static_variables))
            fields.append(
                    '.ctd_container.cod_attributes.static_variables = sipWrappedStaticVariables_' + klass_name)

        if nr_types != 0:
            fields.append(
                    '.ctd_container.cod_attributes.nr_types = ' + str(nr_types))
            fields.append(
                    '.ctd_container.cod_attributes.type_nrs = sipTypeNrs_' + klass_name)


        if has_slots:
            fields.append(
                    '.ctd_container.cod_py_slots = sip_py_slots_' + klass_name)

        # TODO cod_methods (lazy methods so remove?) if not NULL

        # TODO
        #if self.custom_enums_supported() and nrenummembers > 0:
        #    cod_nrenummembers
        #    cod_enummembers

        # TODO
        #if nrvariables > 0:
        #    cod_nrvariables
        #    cod_variables

        fields.append('.ctd_docstring = ' + docstring_ref)

        if klass.metatype is not None:
            fields.append(
                    '.ctd_metatype = ' + self.cached_name_ref(klass.metatype))

        if klass.supertype is not None:
            fields.append(
                    '.ctd_supertype = ' + self.cached_name_ref(klass.supertype))


        # TODO
        #if len(klass.superclasses) != 0:
        #    ctd_supers

        fields.append('.ctd_init = init_type_' + klass_name)

        if self.need_dealloc(bindings, klass):
            fields.append('.ctd_dealloc = dealloc_' + klass_name)

        # TODO
        # ctd_pyslots if there are any (remove?)

        if klass.can_create:
            fields.append(
                    f'.ctd_sizeof = sizeof ({self.scoped_class_name(klass)})')

        if klass.gc_traverse_code is not None:
            fields.append('.ctd_traverse = traverse_' + klass_name)

        if klass.gc_clear_code is not None:
            fields.append('.ctd_clear = clear_' + klass_name)

        if klass.bi_get_buffer_code is not None:
            fields.append('.ctd_getbuffer = getbuffer_' + klass_name)

        if klass.bi_release_buffer_code is not None:
            fields.append('.ctd_releasebuffer = releasebuffer_' + klass_name)

        if spec.c_bindings or klass.needs_copy_helper:
            fields.append('.ctd_assign = assign_' + klass_name)

        if spec.c_bindings or klass.needs_array_helper:
            fields.append('.ctd_array = array_' + klass_name)

        if spec.c_bindings or klass.needs_copy_helper:
            fields.append('.ctd_copy = copy_' + klass_name)

        if not spec.c_bindings and klass.iface_file.type is not IfaceFileType.NAMESPACE:
            fields.append('.ctd_release = release_' + klass_name)

        if len(klass.superclasses) != 0:
            fields.append('.ctd_cast = cast_' + klass_name)

        if klass.convert_to_type_code is not None and klass.iface_file.type is not IfaceFileType.NAMESPACE:
            fields.append('.ctd_cto = convertTo_' + klass_name)

        if klass.convert_from_type_code is not None and klass.iface_file.type is not IfaceFileType.NAMESPACE:
            fields.append('.ctd_cfrom = convertFrom_' + klass_name)

        if klass.pickle_code is not None:
            fields.append('.ctd_pickle = pickle_' + klass_name)

        if klass.finalisation_code is not None:
            fields.append('.ctd_final = final_' + klass_name)

        if klass.mixin:
            fields.append('.ctd_mixin = mixin_' + klass_name)

        if spec.c_bindings or klass.needs_array_helper:
            fields.append('.ctd_array_delete = array_delete_' + klass_name)

        fields = ',\n    '.join(fields)

        sf.write(
f'''

sipClassTypeDef sipTypeDef_{module_name}_{klass_name} = {{
    {fields}
}};
''')

    def g_type_init(self, sf, bindings, klass, need_self, need_owner):
        """ Generate the code that initialises a type. """

        spec = self.spec
        klass_name = klass.iface_file.fq_cpp_name.as_word

        if not spec.c_bindings:
            sf.write(f'extern "C" {{static void *init_type_{klass_name}(sipSimpleWrapper *, PyObject *const *, Py_ssize_t, PyObject *, PyObject **, PyObject **, PyObject **);}}\n')

        sip_owner = 'sipOwner' if need_owner else ''

        sf.write(
f'''static void *init_type_{klass_name}(sipSimpleWrapper *sipSelf, PyObject *const *sipArgs, Py_ssize_t sipNrArgs, PyObject *sipKwds, PyObject **sipUnused, PyObject **{sip_owner}, PyObject **sipParseErr)
{{
''')

        self.g_slot_support_vars(sf)

        self.g_type_init_body(sf, bindings, klass)

        sf.write('}\n')

    def g_type_init_body(self, sf, bindings, klass):
        """ Generate the main body of the type initialisation function. """

        sip_cpp_type = 'sip' + klass_name if klass.has_shadow else self.scoped_class_name(klass)

        sf.write(f'    {sip_cpp_type} *sipCpp = SIP_NULLPTR;\n')

        if bindings.tracing:
            klass_name = klass.iface_file.fq_cpp_name.as_word
            sf.write(f'\n    sipTrace(SIP_TRACE_INITS, "sip_init_instance_{klass_name}()\\n");\n')

        # Generate the code that parses the Python arguments and calls the
        # correct constructor.
        for ctor in klass.ctors:
            if ctor.access_specifier is AccessSpecifier.PRIVATE:
                continue

            sf.write('\n    {\n')

            if ctor.method_code is not None:
                error_flag = need_error_flag(ctor.method_code)
                old_error_flag = self.need_deprecated_error_flag(
                        ctor.method_code)
            else:
                error_flag = old_error_flag = False

            self.g_arg_parser(sf, klass, ctor.py_signature, ctor=ctor)
            self._g_ctor_call(sf, bindings, klass, ctor, error_flag,
                    old_error_flag)

            sf.write('    }\n')

        sf.write(
'''
    return SIP_NULLPTR;
''')

    @classmethod
    def g_types_table(cls, sf, module, needed_enums):
        """ Generate the types table for a module. """

        module_name = module.py_name

        sf.write(
f'''

/*
 * This defines each type in this module.
 */
{cls.get_types_table_prefix()}_{module_name}[] = {{
''')

        # TODO Does this exclude types defined in another module?
        for needed_type in module.needed_types:
            if needed_type.type is ArgumentType.CLASS:
                klass = needed_type.definition

                if klass.external:
                    sf.write('    0,\n')
                elif not klass.is_hidden_namespace:
                    sf.write(f'    &sipTypeDef_{module_name}_{klass.iface_file.fq_cpp_name.as_word}.ctd_base,\n')

            elif needed_type.type is ArgumentType.MAPPED:
                mapped_type = needed_type.definition

                sf.write(f'    &sipTypeDef_{module_name}_{mapped_type.iface_file.fq_cpp_name.as_word}.mtd_base,\n')

            elif needed_type.type is ArgumentType.ENUM:
                enum = needed_type.definition
                enum_index = needed_enums.index(enum)

                sf.write(f'    &enumTypes[{enum_index}].etd_base,\n')

        sf.write('};\n')

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

    def get_class_flags(self, klass, py_debug):
        """ Return the flags for a class. """

        module = self.spec.module
        flags = []

        if klass.is_abstract:
            flags.append('SIP_TYPE_ABSTRACT')

        if klass.subclass_base is not None:
            flags.append('SIP_TYPE_SCC')

        if klass.handles_none:
            flags.append('SIP_TYPE_ALLOW_NONE')

        if klass.has_nonlazy_method:
            flags.append('SIP_TYPE_NONLAZY')

        if module.call_super_init:
            flags.append('SIP_TYPE_SUPER_INIT')

        if not py_debug and module.use_limited_api:
            flags.append('SIP_TYPE_LIMITED_API')

        flags.append('SIP_TYPE_NAMESPACE' if klass.iface_file.type is IfaceFileType.NAMESPACE else 'SIP_TYPE_CLASS')

        return '|'.join(flags)

    def get_class_ref_value(self, klass):
        """ Return the value of a class's reference. """

        return f'SIP_TYPE_ID_GENERATED|SIP_TYPE_ID_CURRENT_MODULE|{klass.iface_file.type_nr}'

    def get_enum_class_scope(self, enum):
        """ Return the scope of an unscoped enum as a string. """

        if enum.is_protected:
            scope_s = 'sip' + enum.scope.iface_file.fq_cpp_name.as_word
        else:
            scope_s = self.scoped_class_name(enum.scope)

        return scope_s

    def get_enum_member(self, enum_member):
        """ Return an enum member as a string. """

        spec = self.spec

        if spec.c_bindings:
            return enum_member.cpp_name

        enum = enum_member.scope

        if enum.no_scope:
            scope_s = ''
        else:
            if enum.is_scoped:
                scope_s = '::' + enum.cached_fq_cpp_name.name
            elif isinstance(enum.scope, WrappedClass):
                scope_s = self.get_enum_class_scope(enum)
            elif isinstance(enum.scope, MappedType):
                scope_s = enum.scope.iface_file.fq_cpp_name.as_cpp
            else:
                # This can't happen.
                scope_s = ''

            scope_s += '::'

        return f'static_cast<int>({scope_s}{enum_member.cpp_name})'

    def get_named_value_decl(self, scope, type, name):
        """ Return the declaration of a named variable to hold a C++ value. """

        saved_derefs = type.derefs
        saved_is_const = type.is_const
        saved_is_reference = type.is_reference

        if len(type.derefs) == 0:
            if type.type in (ArgumentType.CLASS, ArgumentType.MAPPED):
                type.derefs = [False]
            else:
                type.is_const = False

        type.is_reference = False

        named_value_decl = fmt_argument_as_cpp_type(self.spec, type, name=name,
                scope=scope.iface_file if isinstance(scope, (WrappedClass, MappedType)) else None)

        type.derefs = saved_derefs
        type.is_const = saved_is_const
        type.is_reference = saved_is_reference

        return named_value_decl

    @staticmethod
    def get_type_ref(wrapped_object):
        """ Return the reference to the type of a wrapped object. """

        fq_cpp_name = wrapped_object.fq_cpp_name if isinstance(wrapped_object, WrappedEnum) else wrapped_object.iface_file.fq_cpp_name

        return 'sipTypeID_' + fq_cpp_name.as_word

    # The types that need a Python reference.
    _PY_REF_TYPES = (ArgumentType.ASCII_STRING, ArgumentType.LATIN1_STRING,
        ArgumentType.UTF8_STRING, ArgumentType.USTRING, ArgumentType.SSTRING,
        ArgumentType.STRING)

    @staticmethod
    def get_types_table_prefix():
        """ Return the prefix in the name of the wrapped types table. """

        return 'static const sipTypeDef *const sipTypeDefs'

    def keep_py_reference(self, arg):
        """ Return True if the argument has a type that requires an extra
        reference to the originating object to be kept.
        """

        if arg.is_reference or len(arg.derefs) == 0:
            return False

        if arg.type in self._PY_REF_TYPES:
            return True

        # wchar_t strings/arrays don't leak in ABI v14 and later.  Note that
        # this solution could be adopted for earlier ABIs.
        return self.spec.target_abi >= (14, 0) and arg.type is ArgumentType.WSTRING

    def need_dealloc(self, bindings, klass):
        """ Return True if a dealloc function is needed for a class. """

        if klass.iface_file.type is IfaceFileType.NAMESPACE:
            return False

        # Each of these conditions cause some code to be generated.

        if bindings.tracing:
            return True

        if self.spec.c_bindings:
            return True

        if len(klass.dealloc_code) != 0:
            return True

        if klass.dtor is AccessSpecifier.PUBLIC:
            return True

        if klass.has_shadow:
            return True

        return False

    @staticmethod
    def need_deprecated_error_flag(code):
        """ Return True if the deprecated error flag is need by some
        handwritten code.
        """

        # The flag isn't supported.
        return False

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

    def _g_argument_variable(self, sf, scope, arg, arg_nr):
        """ Generate the definition of an argument variable and any supporting
        variables.
        """

        spec = self.spec
        scope_iface_file = scope.iface_file if isinstance(scope, (WrappedClass, MappedType)) else None
        arg_name = fmt_argument_as_name(spec, arg, arg_nr)
        supporting_default_value = ' = 0' if arg.default_value is not None else ''
        nr_derefs = len(arg.derefs)

        if arg.is_in and arg.default_value is not None and arg.type in (ArgumentType.CLASS, ArgumentType.MAPPED) and (nr_derefs == 0 or arg.is_reference):
            arg_cpp_type = fmt_argument_as_cpp_type(spec, arg,
                    scope=scope_iface_file)

            # Generate something to hold the default value as it cannot be
            # assigned straight away.
            expression = fmt_value_list_as_cpp_expression(spec,
                    arg.default_value)
            sf.write(f'        {arg_cpp_type} {arg_name}def = {expression};\n')

        # Adjust the type so we have the type that will really handle it.
        saved_derefs = arg.derefs
        saved_type = arg.type
        saved_is_reference = arg.is_reference
        saved_is_const = arg.is_const

        use_typename = True

        if arg.type in (ArgumentType.ASCII_STRING, ArgumentType.LATIN1_STRING, ArgumentType.UTF8_STRING, ArgumentType.SSTRING, ArgumentType.USTRING, ArgumentType.STRING, ArgumentType.WSTRING):
            if not arg.is_reference:
                if nr_derefs == 2:
                    arg.derefs = arg.derefs[0:1]
                elif nr_derefs == 1 and arg.is_out:
                    arg.derefs = []

        elif arg.type in (ArgumentType.CLASS, ArgumentType.MAPPED, ArgumentType.STRUCT, ArgumentType.UNION, ArgumentType.VOID):
            arg.derefs = [arg.derefs[0] if len(arg.derefs) != 0 else False]

        else:
            arg.derefs = []

            if arg_is_small_enum(arg):
                arg.type = ArgumentType.INT
                use_typename = False

        # Array sizes are always Py_ssize_t.
        if arg.array is ArrayArgument.ARRAY_SIZE:
            arg.type = ArgumentType.SSIZE

        arg.is_reference = False

        if len(arg.derefs) == 0:
            arg.is_const = False

        modified_arg_cpp_type = fmt_argument_as_cpp_type(spec, arg,
                scope=scope_iface_file, use_typename=use_typename)

        sf.write(f'        {modified_arg_cpp_type} {arg_name}')

        # Restore the argument to its original state.
        arg.derefs = saved_derefs
        arg.type = saved_type
        arg.is_reference = saved_is_reference
        arg.is_const = saved_is_const

        # Generate any default value.
        if arg.is_in and arg.default_value is not None:
            sf.write(' = ')

            if arg.type in (ArgumentType.CLASS, ArgumentType.MAPPED) and (nr_derefs == 0 or arg.is_reference):
                sf.write(f'&{arg_name}def')
            else:
                if arg_is_small_enum(arg):
                    sf.write('static_cast<int>(')

                sf.write(
                        fmt_value_list_as_cpp_expression(spec,
                                arg.default_value))

                if arg_is_small_enum(arg):
                    sf.write(')')

        sf.write(';\n')

        # Some types have supporting variables.
        if arg.is_in:
            if arg.get_wrapper:
                sf.write(f'        PyObject *{arg_name}Wrapper{supporting_default_value};\n')
            elif arg.key is not None:
                sf.write(f'        PyObject *{arg_name}Keep{supporting_default_value};\n')

            if arg.type is ArgumentType.CLASS:
                if arg.array is ArrayArgument.ARRAY and self.abi_supports_array():
                    if self.abi_supports_array():
                        sf.write(f'        int {arg_name}IsTemp = 0;\n')
                else:
                    if arg.definition.convert_to_type_code is not None and not arg.is_constrained:
                        sf.write(f'        int {arg_name}State = 0;\n')

                        if type_needs_user_state(arg):
                            sf.write(f'        void *{arg_name}UserState = SIP_NULLPTR;\n')

            elif arg.type is ArgumentType.MAPPED:
                if not arg.definition.no_release and not arg.is_constrained:
                    sf.write(f'        int {arg_name}State = 0;\n')

                    if type_needs_user_state(arg):
                        sf.write(f'        void *{arg_name}UserState = SIP_NULLPTR;\n')

            elif arg.type in (ArgumentType.ASCII_STRING, ArgumentType.LATIN1_STRING, ArgumentType.UTF8_STRING):
                if arg.key is None and nr_derefs == 1:
                    sf.write(f'        PyObject *{arg_name}Keep{supporting_default_value};\n')

    def _g_class_docstring(self, sf, bindings, klass):
        """ Generate the docstring for a class. """

        NEWLINE = '\\n"\n"'

        # See if all the docstrings are automatically generated.
        all_auto = (klass.docstring is None)
        any_implied = False

        for ctor in klass.ctors:
            if ctor.access_specifier is AccessSpecifier.PRIVATE:
                continue

            if ctor.docstring is not None:
                all_auto = False

                if ctor.docstring.signature is not DocstringSignature.DISCARDED:
                    any_implied = True

        # Generate the docstring.
        if all_auto:
            sf.write('\\1')

        if klass.docstring is not None and klass.docstring.signature is not DocstringSignature.PREPENDED:
            sf.write(self.docstring_text(klass.docstring))
            is_first = False
        else:
            is_first = True

        if klass.docstring is None or klass.docstring.signature is not DocstringSignature.DISCARDED:
            for ctor in klass.ctors:
                if ctor.access_specifier is AccessSpecifier.PRIVATE:
                    continue

                if not is_first:
                    sf.write(NEWLINE)

                    # Insert a blank line if any explicit docstring wants to
                    # include a signature.  This maintains compatibility with
                    # previous versions.
                    if any_implied:
                        sf.write(NEWLINE)

                if ctor.docstring is not None:
                    if ctor.docstring.signature is DocstringSignature.PREPENDED:
                        self._g_ctor_auto_docstring(sf, bindings, klass, ctor)
                        sf.write(NEWLINE)

                    sf.write(self.docstring_text(ctor.docstring))

                    if ctor.docstring.signature is DocstringSignature.APPENDED:
                        sf.write(NEWLINE)
                        self._g_ctor_auto_docstring(sf, bindings, klass, ctor)
                elif all_auto or any_implied:
                    self._g_ctor_auto_docstring(sf, bindings, klass, ctor)

                is_first = False

        if klass.docstring is not None and klass.docstring.signature is DocstringSignature.PREPENDED:
            if not is_first:
                sf.write(NEWLINE)
                sf.write(NEWLINE)

            sf.write(self.docstring_text(klass.docstring))

    def _g_ctor_auto_docstring(self, sf, bindings, klass, ctor):
        """ Generate the automatic docstring for a ctor. """

        if bindings.docstrings:
            py_name = fmt_scoped_py_name(klass.scope, klass.py_name.name)
            signature = fmt_signature_as_type_hint(self.spec,
                    ctor.py_signature, need_self=False, exclude_result=True)
            sf.write(py_name + signature)

    def _g_ctor_call(self, sf, bindings, klass, ctor, error_flag,
            old_error_flag):
        """ Generate a single constructor call. """

        spec = self.spec
        klass_name = klass.iface_file.fq_cpp_name.as_word
        scope_s = self.scoped_class_name(klass)

        sf.write('        {\n')

        if ctor.premethod_code is not None:
            sf.write('\n')
            sf.write_code(ctor.premethod_code)
            sf.write('\n')

        if error_flag:
            sf.write('            sipErrorState sipError = sipErrorNone;\n\n')
        elif old_error_flag:
            sf.write('            int sipIsErr = 0;\n\n')

        if ctor.deprecated is not None:
            # Note that any temporaries will leak if an exception is raised.

            if self.abi_has_deprecated_message():
                str_deprecated_message = f'"{ctor.deprecated}"' if ctor.deprecated else 'SIP_NULLPTR'
                sf.write(f'            if (sipDeprecated({self.cached_name_ref(klass.py_name)}, SIP_NULLPTR, {str_deprecated_message}) < 0)\n')
            else:
                sf.write(f'            if (sipDeprecated({self.cached_name_ref(klass.py_name)}, SIP_NULLPTR) < 0)\n')

            sf.write(f'                return SIP_NULLPTR;\n\n')

        # Call any pre-hook.
        if ctor.prehook is not None:
            sf.write(f'            sipCallHook("{ctor.prehook}");\n\n')

        if ctor.method_code is not None:
            sf.write_code(ctor.method_code)
        elif spec.c_bindings:
            sf.write(f'            sipCpp = sipMalloc(sizeof ({scope_s}));\n')
        else:
            rel_gil = release_gil(ctor.gil_action, bindings)

            if ctor.raises_py_exception:
                sf.write('            PyErr_Clear();\n\n')

            if rel_gil:
                sf.write('            Py_BEGIN_ALLOW_THREADS\n')

            self.g_try(sf, bindings, ctor.throw_args)

            klass_type = 'sip' + klass_name if klass.has_shadow else scope_s
            sf.write(f'            sipCpp = new {klass_type}(')

            if ctor.is_cast:
                # We have to fiddle the type to generate the correct code.
                arg0 = ctor.py_signature.args[0]
                saved_definition = arg0.definition
                arg0.definition = klass
                cast_call = fmt_argument_as_cpp_type(spec, arg0)
                arg0.definition = saved_definition

                sf.write(f'a0->operator {cast_call}()')
            else:
                self.g_call_args(sf, ctor.cpp_signature, ctor.py_signature)

            sf.write(');\n')

            self.g_catch(sf, bindings, ctor.py_signature, ctor.throw_args,
                    rel_gil)

            if rel_gil:
                sf.write('            Py_END_ALLOW_THREADS\n')

            # This is a bit of a hack to say we want the result transferred.
            # We don't simply call sipTransferTo() because the wrapper object
            # hasn't been fully initialised yet.
            if ctor.transfer is Transfer.TRANSFER:
                sf.write('\n            *sipOwner = Py_None;\n')

        # Handle any /KeepReference/ arguments.
        for arg_nr, arg in enumerate(ctor.py_signature.args):
            if not arg.is_in:
                continue

            if arg.key is not None:
                arg_name = fmt_argument_as_name(spec, arg, arg_nr)
                suffix = 'Keep' if (arg.type in (ArgumentType.ASCII_STRING, ArgumentType.LATIN1_STRING, ArgumentType.UTF8_STRING) and len(arg.derefs) == 1) or not arg.get_wrapper else 'Wrapper'

                sf.write(f'\n            sipKeepReference((PyObject *)sipSelf, {arg.key}, {arg_name}{suffix});\n')

        self.g_gc_ellipsis(sf, ctor.py_signature)
        self.g_delete_temporaries(sf, ctor.py_signature)

        sf.write('\n')

        if ctor.raises_py_exception:
            sf.write(
'''            if (PyErr_Occurred())
            {
                delete sipCpp;
                return SIP_NULLPTR;
            }

''')

        if error_flag:
            sf.write('            if (sipError == sipErrorNone)\n')

            if klass.has_shadow or ctor.posthook is not None:
                sf.write('            {\n')

            if klass.has_shadow:
                sf.write('                sipCpp->sipPySelf = sipSelf;\n\n')

            # Call any post-hook.
            if ctor.posthook is not None:
                sf.write(f'            sipCallHook("{ctor.posthook}");\n\n')

            sf.write('                return sipCpp;\n')

            if klass.has_shadow or ctor.posthook is not None:
                sf.write('            }\n')

            sf.write(
'''
            if (sipUnused)
            {
                Py_XDECREF(*sipUnused);
            }

            sipAddException(sipError, sipParseErr);

            if (sipError == sipErrorFail)
                return SIP_NULLPTR;
''')
        else:
            if old_error_flag:
                sf.write(
'''            if (sipIsErr)
            {
                if (sipUnused)
                {
                    Py_XDECREF(*sipUnused);
                }

                sipAddException(sipErrorFail, sipParseErr);
                return SIP_NULLPTR;
            }

''')

            if klass.has_shadow:
                sf.write('            sipCpp->sipPySelf = sipSelf;\n\n')

            # Call any post-hook.
            if ctor.posthook is not None:
                sf.write(f'            sipCallHook("{ctor.posthook}");\n\n')

            sf.write('            return sipCpp;\n')

        sf.write('        }\n')

    def _g_delete_outs(self, sf, py_signature):
        """ Generate the code to delete any instances created to hold /Out/
        arguments.
        """

        for arg_nr, arg in enumerate(py_signature.args):
            if arg.type in (ArgumentType.CLASS, ArgumentType.MAPPED) and _need_new_instance(arg):
                sf.write(f'                delete {fmt_argument_as_name(self.spec, arg, arg_nr)};\n')

    def _g_method_auto_docstring(self, sf, bindings, overload, is_method):
        """ Generate the automatic docstring for a function/method. """

        if bindings.docstrings:
            self._g_overload_auto_docstring(sf, overload, is_method=is_method)

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

        # Note that this will be called if wrapped_module_exec() fails in any
        # way, so we can't assume the sip API is available.
        sf.write(
'''

/* The wrapped module's free slot. */
static void wrapped_module_free(void *wmod_ptr)
{
    sipWrappedModuleState *wms = (sipWrappedModuleState *)PyModule_GetState(
            (PyObject *)wmod_ptr);

    if (wms->sip_api != NULL)
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
                        has_method_docstring(bindings, member,
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

    def _g_overload_auto_docstring(self, sf, overload, is_method=True):
        """ Generate the docstring for a single API overload. """

        need_self = is_method and not overload.is_static
        signature = fmt_signature_as_type_hint(self.spec,
                overload.py_signature, need_self=need_self)
        sf.write(overload.common.py_name.name + signature)

    def _g_py_method_table(self, sf, bindings, members, scope):
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
            comma = '' if member is members[-1] else ','

            if member.no_arg_parser or member.allow_keyword_args:
                cast = 'SIP_MLMETH_CAST('
                cast_suffix = ')'
                flags = '|METH_KEYWORDS'
            else:
                cast = ''
                cast_suffix = ''
                flags = ''

            if has_method_docstring(bindings, member, scope.overloads):
                docstring = f'doc_{scope_name}_{py_name.name}'
            else:
                docstring = 'SIP_NULLPTR'

            if no_intro:
                sf.write(
f'''

static PyMethodDef methods_{scope_name}[] = {{
''')

                no_intro = False

            sf.write(f'    {{{cached_py_name}, {cast}meth_{scope_name}_{py_name.name}{cast_suffix}, METH_VARARGS{flags}, {docstring}}}{comma}\n')

        if not no_intro:
            sf.write('};\n')

        return len(members)

    def _g_pyqt_emitters(self, sf, klass):
        """ Generate the PyQt emitters for a class. """

        spec = self.spec
        klass_name = klass.iface_file.fq_cpp_name.as_word
        scope_s = self.scoped_class_name(klass)
        klass_name_ref = self.cached_name_ref(klass.py_name)

        for member in klass.members:
            in_emitter = False

            for overload in klass.overloads:
                if not (overload.common is member and overload.pyqt_method_specifier is PyQtMethodSpecifier.SIGNAL and _has_optional_args(overload)):
                    continue

                if not in_emitter:
                    in_emitter = True

                    sf.write('\n\n')

                    if not spec.c_bindings:
                        sf.write(f'extern "C" {{static int emit_{klass_name}_{overload.cpp_name}(void *, PyObject *);}}\n\n')

                    sf.write(
f'''static int emit_{klass_name}_{overload.cpp_name}(void *sipCppV, PyObject *sipArgs)
{{
    PyObject *sipParseErr = SIP_NULLPTR;
    {scope_s} *sipCpp = reinterpret_cast<{scope_s} *>(sipCppV);
''')

                # Generate the code that parses the args and emits the
                # appropriate overloaded signal.
                sf.write('\n    {\n')

                self.g_arg_parser(sf, klass, overload.py_signature)

                sf.write(
f'''        {{
            Py_BEGIN_ALLOW_THREADS
            sipCpp->{overload.cpp_name}(''')

                self.g_call_args(sf, overload.cpp_signature,
                        overload.py_signature)

                sf.write(''');
            Py_END_ALLOW_THREADS

''')

                self.g_delete_temporaries(sf, overload.py_signature)

                sf.write(
'''
            return 0;
        }
    }
''')

            if in_emitter:
                member_name_ref = self.cached_name_ref(member.py_name)

                sf.write(
f'''
    sipNoMethod(sipParseErr, {klass_name_ref}, {member_name_ref}, SIP_NULLPTR);

    return -1;
}}
''')

    def _g_pyqt_signal_table_entry(self, sf, bindings, klass, signal,
            member_nr):
        """ Generate an entry in the PyQt signal table. """

        spec = self.spec
        klass_name = klass.iface_file.fq_cpp_name.as_word

        stripped = False
        signature_state = {}

        args = []

        for arg in signal.cpp_signature.args:
            # Do some signal argument normalisation so that Qt doesn't have to.
            if arg.is_const and (arg.is_reference or len(arg.derefs) == 0):
                signature_state[arg] = arg.is_reference

                arg.is_const = False
                arg.is_reference = False

            if arg.scopes_stripped != 0:
                strip = arg.scopes_stripped
                stripped = True
            else:
                strip = STRIP_GLOBAL

            args.append(
                    fmt_argument_as_cpp_type(spec, arg, scope=klass.iface_file,
                            strip=strip))

        # Note the lack of a separating space.
        args = ','.join(args)

        sf.write(f'    {{"{signal.cpp_name}({args})')

        # If a scope was stripped then append an unstripped version which can
        # be parsed by PyQt.
        if stripped:
            args = []

            for arg in signal.cpp_signature.args:
                args.append(
                        fmt_argument_as_cpp_type(spec, arg,
                                scope=klass.iface_file, strip=STRIP_GLOBAL))

            # Note the lack of a separating space.
            args = ','.join(args)

            sf.write(f'|({args})')

        sf.write('", ')

        # Restore the signature state.
        for arg, is_reference in signature_state.items():
            arg.is_const = True
            arg.is_reference = is_reference

        if bindings.docstrings:
            sf.write('"')

            if signal.docstring is not None:
                if signal.docstring.signature is DocstringSignature.PREPENDED:
                    self._g_overload_auto_docstring(sf, signal)
                    sf.write('\\n')

                sf.write(self.docstring_text(signal.docstring))

                if signal.docstring.signature is DocstringSignature.APPENDED:
                    sf.write('\\n')
                    self._g_overload_auto_docstring(sf, signal)
            else:
                sf.write('\\1')
                self._g_overload_auto_docstring(sf, signal)

            sf.write('", ')
        else:
            sf.write('SIP_NULLPTR, ')

        sf.write(f'&methods_{klass_name}[{member_nr}], ' if member_nr >= 0 else 'SIP_NULLPTR, ')

        sf.write(f'emit_{klass_name}_{signal.cpp_name}' if _has_optional_args(signal) else 'SIP_NULLPTR')

        sf.write('},\n')

    def _g_pyqt_signals_table(self, sf, bindings, klass):
        """ Generate the PyQt signals table and return True if anything was
        generated.
        """

        # Handle the trivial case.
        if not klass.is_qobject:
            return False

        spec = self.spec

        is_signals = False

        # The signals must be grouped by name.
        for member in klass.members:
            member_nr = member.member_nr

            for overload in klass.overloads:
                if overload.common is not member or overload.pyqt_method_specifier is not PyQtMethodSpecifier.SIGNAL:
                    continue

                if member_nr >= 0:
                    # See if there is a non-signal overload.
                    for non_sig in klass.overloads:
                        if non_sig is not overload and non_sig.common is member and non_sig.pyqt_method_specifier is not PyQtMethodSpecifier.SIGNAL:
                            break
                    else:
                        member_nr = -1

                if not is_signals:
                    is_signals = True

                    self._g_pyqt_emitters(sf, klass)

                    pyqt_version = '5' if self.pyqt5_supported() else '6'
                    sf.write(
f'''

/* Define this type's signals. */
static const pyqt{pyqt_version}QtSignal signals_{klass.iface_file.fq_cpp_name.as_word}[] = {{
''')

                # We enable a hack that supplies any missing optional
                # arguments.  We only include the version with all arguments
                # and provide an emitter function which handles the optional
                # arguments.
                self._g_pyqt_signal_table_entry(sf, bindings, klass, overload,
                        member_nr)

                member_nr = -1

        if is_signals:
            sf.write('    {SIP_NULLPTR, SIP_NULLPTR, SIP_NULLPTR, SIP_NULLPTR}\n};\n')

        return is_signals

    def _g_variables_table(self, sf, scope, *, for_static):
        """ Generate the table of either static or instance variables for a
        scope and return the length of the table.
        """

        c_bindings = self.spec.c_bindings
        module = self.spec.module

        if scope is None:
            scope_type = 'module'
            suffix = module.py_name
        else:
            scope_type = 'type'
            suffix = scope.iface_file.fq_cpp_name.as_word

        nr_variables = 0

        # Get the sorted list of variables.
        variables = list(self.variables_in_scope(scope, check_handler=False))
        variables.sort(key=lambda k: k.py_name.name)

        # Generate any %GetCode and %SetCode wrappers.
        for variable in variables:
            v_ref = variable.fq_cpp_name.as_word

            if variable.get_code is not None:
                # TODO Support sipPyType when scope is not None.
                # TODO Review the need to cache class instances (see legacy
                # variable handlers).  Or is that now in the sip module
                # wrapper?
                sf.write('\n')

                if not c_bindings:
                    sf.write(f'extern "C" {{static PyObject *sipWrappedVariableGetCode_{v_ref}();}}\n')

                sf.write(
f'''static PyObject *sipWrappedVariableGetCode_{v_ref}()
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
                    sf.write(f'extern "C" {{static int sipWrappedVariableSetCode_{v_ref}(PyObject *);}}\n')

                sf.write(
f'''static int sipWrappedVariableSetCode_{v_ref}(PyObject *sipPy)
{{
    int sipErr = 0;

''')

                sf.write_code(variable.set_code)

                sf.write(
'''
    return sipErr ? -1 : 0;
}

''')

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

            if nr_variables == 0:
                if scope is None:
                    v_type = 'Module'
                elif variable.is_static:
                    v_type = 'Static'
                else:
                    v_type = 'Instance'

                sf.write(
f'''
/* Define the {v_type.lower()} variables for the {scope_type}. */
static const sipWrappedVariableDef sipWrapped{v_type}Variables_{suffix}[] = {{
''')

            name = variable.py_name

            if not_settable or variable.no_setter:
                key = 'SIP_WV_RO'
            elif might_need_key:
                key = module.next_key
                module.next_key -= 1
            else:
                key = '0'

            if scope is None or variable.is_static:
                # TODO Why STRIP_GLOBAL here in particular?
                cpp_name = variable.fq_cpp_name.cpp_stripped(STRIP_GLOBAL)
                addr = f'(void *)&{cpp_name}'
            else:
                addr = 'SIP_NULLPTR'

            v_ref = variable.fq_cpp_name.as_word
            getter = self.optional_ptr(variable.get_code is not None,
                    f'sipWrappedVariableGetCode_{v_ref}')
            setter = self.optional_ptr(variable.set_code is not None,
                    f'sipWrappedVariableSetCode_{v_ref}')

            sf.write(f'    {{"{name}", {type_id}, {key}, {addr}, {getter}, {setter}}},\n')

            nr_variables += 1

        if nr_variables != 0:
            sf.write('};\n')

        return nr_variables


def _get_function_table(members):
    """ Return a sorted list of relevant functions for a namespace. """

    return sorted(members, key=lambda m: m.py_name.name)


def _get_method_table(klass):
    """ Return a sorted list of relevant methods (either lazy or non-lazy) for
    a class.
    """

    # Only provide an entry point if there is at least one overload that is
    # defined in this class and is a non-abstract function or slot.  We allow
    # private (even though we don't actually generate code) because we need to
    # intercept the name before it reaches a more public version further up the
    # class hierarchy.  We add the ctor and any variable handlers as special
    # entries.

    members = []

    for visible_member in klass.visible_members:
        if visible_member.member.py_slot is not None:
            continue

        need_member = False

        for overload in visible_member.scope.overloads:
            # Skip protected methods if we don't have the means to handle them.
            if overload.access_specifier is AccessSpecifier.PROTECTED and not klass.has_shadow:
                continue

            if not skip_overload(overload, visible_member.member, klass, visible_member.scope):
                need_member = True

        if need_member:
            members.append(visible_member.member)

    return _get_function_table(members)


def _get_subformat_char(arg):
    """ Return the sub-format character for an argument. """

    flags = 0

    if arg.transfer is Transfer.TRANSFER:
        flags |= 0x02

    if arg.transfer is Transfer.TRANSFER_BACK:
        flags |= 0x04

    if arg.type in (ArgumentType.CLASS, ArgumentType.MAPPED):
        if len(arg.derefs) == 0 or arg.disallow_none:
            flags |= 0x01

        if arg.transfer is Transfer.TRANSFER_THIS:
            flags |= 0x10

        if arg.is_constrained or (arg.type is ArgumentType.CLASS and arg.definition.convert_to_type_code is None):
            flags |= 0x08

    return chr(ord('0') + flags)


def _has_class_docstring(bindings, klass):
    """ Return True if a class has a docstring. """

    auto_docstring = False

    # Check for any explicit docstrings and remember if there were any that
    # could be automatically generated.
    if klass.docstring is not None:
        return True

    for ctor in klass.ctors:
        if ctor.access_specifier is AccessSpecifier.PRIVATE:
            continue

        if ctor.docstring is not None:
            return True

        if bindings.docstrings:
            auto_docstring = True

    if not klass.can_create:
        return False

    return auto_docstring


def _handling_exceptions(bindings, throw_args):
    """ Return True if exceptions from a callable are being handled. """

    # Handle any exceptions if there was no throw specifier, or a non-empty
    # throw specifier.
    return bindings.exceptions and (throw_args is None or throw_args.arguments is not None)


def _has_optional_args(overload):
    """ Return True if an overload has optional arguments. """

    args = overload.cpp_signature.args

    return len(args) != 0 and args[-1].default_value is not None
