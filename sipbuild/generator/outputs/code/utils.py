# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from ...specification import (AccessSpecifier, ArgumentType, CodeBlock,
        GILAction, PyQtMethodSpecifier, WrappedClass)


# TODO Review these for ones that are only needed by one backend.
def arg_is_small_enum(arg):
    """ Return True if an argument refers to a small C++11 enum. """

    return arg.type is ArgumentType.ENUM and arg.definition.enum_base_type is not None


def callable_overloads(member, overloads):
    """ An iterator over the non-private and non-signal overloads. """

    for overload in overloads:
        if overload.common is member and overload.access_specifier is not AccessSpecifier.PRIVATE and overload.pyqt_method_specifier is not PyQtMethodSpecifier.SIGNAL:
            yield overload


def get_convert_to_type_code(type):
    """ Return a type's %ConvertToTypeCode. """

    if type.type in (ArgumentType.CLASS, ArgumentType.MAPPED) and not type.is_constrained:
        return type.definition.convert_to_type_code

    return None


def get_encoded_type(module, klass, last=False):
    """ Return the structure representing an encoded type. """

    klass_module = klass.iface_file.module

    fields = [str(klass.iface_file.type_nr)]

    if klass_module is module:
        fields.append('255')
    else:
        for module_nr, imported_module in enumerate(module.all_imports):
            if imported_module is klass_module:
                fields.append(str(module_nr))
                break

    fields.append(str(int(last)))

    return '{' + ', '.join(fields) + '}'


def get_normalised_cached_name(cached_name):
    """ Return the normalised form of a cached name. """

    # If the name seems to be a template then just use the offset to ensure
    # that it is unique.
    if '<' in cached_name.name:
        return str(cached_name.offset)

    # Handle C++ and Python scopes.
    return cached_name.name.replace(':', '_').replace('.', '_')


def get_slot_name(slot_type):
    """ Return the sip module's string equivalent of a slot. """

    return slot_type.name.lower() + '_slot'


def get_type_from_void(spec, type_name, variable_name, tight=False):
    """ Return a cast from a void * variable to a pointer type. """

    if spec.c_bindings:
        if tight:
            return f'(({type_name} *){variable_name})'

        return f'({type_name} *){variable_name}'

    return f'reinterpret_cast<{type_name} *>({variable_name})'


def get_user_state_suffix(spec, type):
    """ Return the suffix for functions that have a variant that supports a
    user state.
    """

    return 'US' if spec.target_abi >= (13, 0) and type_needs_user_state(type) else ''


def has_method_docstring(bindings, member, overloads):
    """ Return True if a function/method has a docstring. """

    auto_docstring = False

    # Check for any explicit docstrings and remember if there were any that
    # could be automatically generated.
    for overload in callable_overloads(member, overloads):
        if overload.docstring is not None:
            return True

        if bindings.docstrings:
            auto_docstring = True

    if member.no_arg_parser:
        return False

    return auto_docstring


def is_used_in_code(code, s):
    """ Return True if a string is used in code. """

    # The code may be a list of code blocks or an optional code block.
    if code is None:
        return False

    if isinstance(code, CodeBlock):
        code = [code]

    for cb in code:
        if s in cb.text:
            return True

    return False


def need_error_flag(code):
    """ Return True if handwritten code uses the error flag. """

    return is_used_in_code(code, 'sipError')


def py_scope(scope):
    """ Return the Python scope by accounting for hidden C++ namespaces. """

    return None if isinstance(scope, WrappedClass) and scope.is_hidden_namespace else scope


def release_gil(gil_action, bindings):
    """ Return True if the GIL is to be released. """

    return bindings.release_gil if gil_action is GILAction.DEFAULT else gil_action is GILAction.RELEASE


def skip_overload(overload, member, klass, scope, want_local=True):
    """ See if a member overload should be skipped. """

    # Skip if it's not the right name.
    if overload.common is not member:
        return True

    # Skip if it's a signal.
    if overload.pyqt_method_specifier is PyQtMethodSpecifier.SIGNAL:
        return True

    # Skip if it's a private abstract.
    if overload.is_abstract and overload.access_specifier is AccessSpecifier.PRIVATE:
        return True

    # If we are disallowing them, skip if it's not in the current class unless
    # it is protected.
    if want_local and overload.access_specifier is not AccessSpecifier.PROTECTED and klass is not scope:
        return True

    return False


def type_needs_user_state(type):
    """ Return True if a type needs user state to be provided. """

    return type.type is ArgumentType.MAPPED and type.definition.needs_user_state
