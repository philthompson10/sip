# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from ...specification import (AccessSpecifier, PyQtMethodSpecifier,
        WrappedClass)


def callable_overloads(member, overloads):
    """ An iterator over the non-private and non-signal overloads. """

    for overload in overloads:
        if overload.common is member and overload.access_specifier is not AccessSpecifier.PRIVATE and overload.pyqt_method_specifier is not PyQtMethodSpecifier.SIGNAL:
            yield overload


def get_normalised_cached_name(cached_name):
    """ Return the normalised form of a cached name. """

    # If the name seems to be a template then just use the offset to ensure
    # that it is unique.
    if '<' in cached_name.name:
        return str(cached_name.offset)

    # Handle C++ and Python scopes.
    return cached_name.name.replace(':', '_').replace('.', '_')


def has_member_docstring(bindings, member, overloads):
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


def py_scope(scope):
    """ Return the Python scope by accounting for hidden C++ namespaces. """

    return None if isinstance(scope, WrappedClass) and scope.is_hidden_namespace else scope
