# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2024 Phil Thompson <phil@riverbankcomputing.com>


from ...python_slots import is_multi_arg_slot, is_number_slot
from ...specification import (AccessSpecifier, ArgumentType, ArrayArgument,
        IfaceFileType, KwArgs, MappedType, PySlot, Transfer, WrappedClass)

from ..formatters import (fmt_argument_as_cpp_type, fmt_argument_as_name,
        fmt_value_list_as_cpp_expression)

from .utils import (abi_supports_array, cached_name_ref, get_gto_name,
        is_string, scoped_class_name, type_needs_user_state)


def argument_parser(sf, spec, scope, py_signature, ctor=None, overload=None):
    """ Generate the argument variables for a member
    function/constructor/operator.
    """

    # If the scope is just a namespace, then ignore it.
    if isinstance(scope, WrappedClass) and scope.iface_file.type is IfaceFileType.NAMESPACE:
        scope = None

    # For ABI v13 and later static methods use self for the type object.
    if spec.abi_version >= (13, 0):
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

        _argument_variable(sf, spec, scope, arg, arg_nr)

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
            cpp_type += scoped_class_name(spec, scope)

        sf.write(f'        {cpp_type} *sipCpp;\n\n')
    elif len(py_signature.args) != 0:
        sf.write('\n')

    # Generate the call to the parser function.
    args = []
    single_arg = False

    if overload is not None and is_number_slot(overload.common.py_slot):
        parser_function = 'sipParsePair'
        args.append('&sipParseErr')
        args.append('sipArg0')
        args.append('sipArg1')

    elif overload is not None and overload.common.py_slot is PySlot.SETATTR:
        # We don't even try to invoke the parser if there is a value and there
        # shouldn't be (or vice versa) so that the list of errors doesn't get
        # polluted with signatures that can never apply.
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
        # We handle keywords if we might have been passed some (because one of
        # the overloads uses them or we are a ctor).  However this particular
        # overload might not have any.
        if overload is not None:
            kw_args = overload.kw_args
        elif ctor is not None:
            kw_args = ctor.kw_args
        else:
            kw_args = KwArgs.NONE

        # The above test isn't good enough because when the flags were set in
        # the parser we couldn't know for sure if an argument was an output
        # pointer.  Therefore we check here.  The drawback is that we may
        # generate the name string for the argument but never use it, or we
        # might have an empty keyword name array or one that contains only
        # NULLs.
        is_ka_list = False

        if kw_args is not KwArgs.NONE:
            for arg in py_signature.args:
                if not arg.is_in:
                    continue

                if not is_ka_list:
                    sf.write('        static const char *sipKwdList[] = {\n')
                    is_ka_list = True

                if arg.name is not None and (kw_args is KwArgs.ALL or arg.default_value is not None):
                    arg_name_ref = cached_name_ref(arg.name)
                else:
                    arg_name_ref = 'SIP_NULLPTR'

                sf.write(f'            {arg_name_ref},\n')

            if is_ka_list:
                sf.write('        };\n\n')

        parser_function = 'sipParseKwdArgs'
        args.append('sipParseErr' if ctor is not None else '&sipParseErr')
        args.append('sipArgs')
        args.append('sipKwds')
        args.append('sipKwdList' if is_ka_list else 'SIP_NULLPTR')
        args.append('sipUnused' if ctor is not None else 'SIP_NULLPTR')

    else:
        single_arg = not (overload is None or overload.common.py_slot is None or is_multi_arg_slot(overload.common.py_slot))
        plural = '' if single_arg else 's'

        parser_function = 'sipParseArgs'
        args.append('&sipParseErr')
        args.append('sipArg' + plural)

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
        # reference to.  However if it is an encoded string then we will get
        # the actual wrapper from the format character.
        if arg.get_wrapper:
            format_s += '@'
        elif arg.key is not None:
            if not (arg.type in (ArgumentType.ASCII_STRING, ArgumentType.LATIN1_STRING, ArgumentType.UTF8_STRING) and len(arg.derefs) == 1):
                format_s += '@'

        if arg.type is ArgumentType.ASCII_STRING:
            format_s += 'AA' if is_string(arg) else 'aA'

        elif arg.type is ArgumentType.LATIN1_STRING:
            format_s += 'AL' if is_string(arg) else 'aL'

        elif arg.type is ArgumentType.UTF8_STRING:
            format_s += 'A8' if is_string(arg) else 'a8'

        elif arg.type in (ArgumentType.SSTRING, ArgumentType.USTRING, ArgumentType.STRING):
            if arg.array is ArrayArgument.ARRAY:
                format_s += 'k'
            elif is_string(arg):
                format_s += 's'
            else:
                format_s += 'c'

        elif arg.type is ArgumentType.WSTRING:
            if arg.array is ArrayArgument.ARRAY:
                format_s += 'K'
            elif is_string(arg):
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

        elif arg.type in (ArgumentType.BYTE, ArgumentType.SBYTE):
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
                format_s += '>' if arg.type is ArgumentType.CLASS and abi_supports_array(spec) else 'r'
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
            args.append(get_gto_name(scope))
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

            args.append(get_gto_name(mapped_type))
            args.append(arg_name_ref)

            if arg.array is ArrayArgument.ARRAY:
                array_len_arg_name = fmt_argument_as_name(spec,
                        py_signature.args[array_len_arg_nr], array_len_arg_nr)
                args.append('&' + array_len_arg_name)
            elif mapped_type.convert_to_type_code is not None and not arg.is_constrained:
                args.append('SIP_NULLPTR' if mapped_type.no_release else f'&{arg_name}State')

                if mapped_type.needs_user_state:
                    args.append(f'&{arg_name}UserState')

        elif arg.type is ArgumentType.CLASS:
            klass = arg.definition

            args.append(get_gto_name(klass))
            args.append(arg_name_ref)

            if arg.array is ArrayArgument.ARRAY:
                array_len_arg_name = fmt_argument_as_name(spec,
                        py_signature.args[array_len_arg_nr], array_len_arg_nr)
                args.append('&' + array_len_arg_name)

                if abi_supports_array(spec):
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
                args.append(get_gto_name(arg.definition))

            args.append(arg_name_ref)

        elif arg.type is ArgumentType.CAPSULE:
            args.append('"' + arg.definition.as_cpp + '"')
            args.append(arg_name_ref)

        else:
            if arg.array is not ArrayArgument.ARRAY_SIZE:
                args.append(arg_name_ref)

            if arg.array is ArrayArgument.ARRAY:
                array_len_arg_name = fmt_argument_as_name(spec,
                        py_signature.args[array_len_arg_nr], array_len_arg_nr)
                args.append('&' + array_len_arg_name)

    args = ', '.join(args)

    sf.write(f'        if ({parser_function}({args}))\n')


def _argument_variable(sf, spec, scope, arg, arg_nr):
    """ Generate the definition of an argument variable and any supporting
    variables.
    """

    scope_iface_file = scope.iface_file if isinstance(scope, (WrappedClass, MappedType)) else None
    arg_name = fmt_argument_as_name(spec, arg, arg_nr)
    supporting_default_value = ' = 0' if arg.default_value is not None else ''
    nr_derefs = len(arg.derefs)

    if arg.is_in and arg.default_value is not None and arg.type in (ArgumentType.CLASS, ArgumentType.MAPPED) and (nr_derefs == 0 or arg.is_reference):
        arg_cpp_type = fmt_argument_as_cpp_type(spec, arg,
                scope=scope_iface_file)

        # Generate something to hold the default value as it cannot be assigned
        # straight away.
        expression = fmt_value_list_as_cpp_expression(spec, arg.default_value)
        sf.write(f'        {arg_cpp_type} {arg_name}def = {expression};\n')

    # Adjust the type so we have the type that will really handle it.
    saved_derefs = arg.derefs
    saved_type = arg.type
    saved_is_reference = arg.is_reference
    saved_is_const = arg.is_const

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

    # Array sizes are always Py_ssize_t.
    if arg.array is ArrayArgument.ARRAY_SIZE:
        arg.type = ArgumentType.SSIZE

    arg.is_reference = False

    if len(arg.derefs) == 0:
        arg.is_const = False

    modified_arg_cpp_type = fmt_argument_as_cpp_type(spec, arg,
            scope=scope_iface_file)

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
            sf.write(fmt_value_list_as_cpp_expression(spec, arg.default_value))

    sf.write(';\n')

    # Some types have supporting variables.
    if arg.is_in:
        if arg.get_wrapper:
            sf.write(f'        PyObject *{arg_name}Wrapper{supporting_default_value};\n')
        elif arg.key is not None:
            sf.write(f'        PyObject *{arg_name}Keep{supporting_default_value};\n')

        if arg.type is ArgumentType.CLASS:
            if arg.array is ArrayArgument.ARRAY and abi_supports_array(spec):
                if abi_supports_array(spec):
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
