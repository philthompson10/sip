# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2024 Phil Thompson <phil@riverbankcomputing.com>


from .outputs.formatters import (fmt_argument_as_cpp_type, fmt_docstring,
        fmt_docstring_of_overload)
from .parser import (InvalidAnnotation, validate_boolean, validate_integer,
        validate_string_list, validate_string)
from .specification import (GILAction, MappedType, Module, Overload,
        WrappedClass)
from .utils import get_c_ref


class BuildSystemExtension:
    """ The base class for a build system extension.  An extension uses opaque
    representations of a scope (either a module, class or mapped type), an
    exception, a function, a function group (a sequence of functions with the
    same Python name), a ctor, a dtor, a union, a namespace, a typedef, an
    argument, a variable, an enum and an enum member.
    """

    def __init__(self, name, bindings, spec):
        """ Initialise the extension. """

        self.name = name
        self.bindings = bindings

        self._spec = spec

    def get_argument_cpp_decl(self, argument, scope, strip=0):
        """ Returns the C++ declaration of an argument within a specific scope.
        If the name of the argument type contains scopes then 'strip' specifies
        the number of leading scopes to be removed.  If it is -1 then only the
        leading global scope is removed.
        """

        iface_file = None if isinstance(scope, Module) else scope.iface_file

        return fmt_argument_as_cpp_type(self._spec, argument, scope=iface_file,
                strip=strip)

    def get_extension_data(self, extendable, factory=None):
        """ Return the build system extension-specific extension data for an
        extendeable object, optionally creating it if necessary.
        """

        if extendable.extension_data is None:
            if factory is None:
                return None

            extendable.extension_data = {}
        else:
            try:
                return extendable.extension_data[self.name]
            except KeyError:
                pass

            if factory is None:
                return None

        extension_data = factory()
        extendable.extension_data[self.name] = extension_data

        return extension_data

    @staticmethod
    def get_class_fq_cpp_name(klass):
        """ Return the fully qualified C++ name of a class. """

        return klass.iface_file.fq_cpp_name.as_cpp

    @staticmethod
    def get_function_cpp_arguments(function):
        """ Return a sequence of the C++ arguments of a function. """

        return function.cpp_signature.args

    @staticmethod
    def get_function_cpp_name(function):
        """ Return the C++ name of a function. """

        return function.cpp_name

    @staticmethod
    def get_function_group_bindings(function_group, scope):
        """ Return a reference to the generated function that implements the
        bindings of a function group.
        """

        return get_c_ref('meth', scope, function_group[0].common.py_name.name)

    @staticmethod
    def parse_boolean_annotation(name, raw_value, location):
        """ Parse and return the valid value of a boolean annotation. """

        pm, p, symbol = location

        try:
            value = validate_boolean(pm, p, symbol, name, raw_value)
        except InvalidAnnotation as e:
            pm.parser_error(p, symbol, str(e))
            value = e.use

        return value

    @staticmethod
    def parse_integer_annotation(name, raw_value, location):
        """ Parse and return the valid value of an integer annotation. """

        pm, p, symbol = location

        try:
            value = validate_integer(pm, p, symbol, name, raw_value,
                    optional=False)
        except InvalidAnnotation as e:
            pm.parser_error(p, symbol, str(e))
            value = e.use

        return value

    @staticmethod
    def parse_string_annotation(name, raw_value, location):
        """ Parse and return the valid value of a string annotation. """

        pm, p, symbol = location

        try:
            value = validate_string(pm, p, symbol, name, raw_value)
        except InvalidAnnotation as e:
            pm.parser_error(p, symbol, str(e))
            value = e.use

        return value

    @staticmethod
    def parse_string_list_annotation(name, raw_value, location):
        """ Parse and return the valid value of a string list annotation. """

        pm, p, symbol = location

        try:
            value = validate_string_list(pm, p, symbol, name, raw_value)
        except InvalidAnnotation as e:
            pm.parser_error(p, symbol, str(e))
            value = e.use

        return value

    @staticmethod
    def parsing_error(error_message, location):
        """ Register a parsing error at a particular location. """

        pm, p, symbol = location

        pm.parser_error(p, symbol, error_message)

    @staticmethod
    def query_argument_is_optional(argument):
        """ Returns True if the argument is optional. """

        return argument.default_value is not None

    @staticmethod
    def query_class_is_subclass(klass, module_name, class_name):
        """ Return True if a class with the given name is the same as, or is a
        subclass of the class.
        """

        for klass in klass.mro:
            if klass.iface_file.module.fq_py_name.name == module_name and klass.py_name.name == class_name:
                return True

        return False

    @staticmethod
    def query_function_has_method_code(function):
        """ Return True if a function has %MethodCode. """

        return function.method_code is not None

    @staticmethod
    def query_scope_is_class(scope):
        """ Return True if a scope is a class. """

        return isinstance(scope, WrappedClass)

    @staticmethod
    def query_scope_is_mapped_type(scope):
        """ Return True if a scope is a mapped type. """

        return isinstance(scope, MappedType)

    @staticmethod
    def query_scope_is_module(scope):
        """ Return True if a scope is a module. """

        return isinstance(scope, Module)

    @staticmethod
    def set_function_release_gil(function):
        """ Apply the /ReleaseGIL/ annotation to a function. """

        function.gil_action = GILAction.RELEASE

    def write_function_group_bindings(self, function_group, scope, output,
            prefix=''):
        """ Write the C/C++ function of type PyCFunction that implements the
        bindings of a function group.  Return a reference to the generated
        function (preceded with an optional prefix).
        """

        # XXX
        return ''

    # The rest of the class are the stubs to be re-implemented by sub-classes.
    # There is a naming convention that splits the name into three broad
    # sections.  The first is the type of object that is the subject of the
    # call, the second is the generic action and the third describes the
    # detail.

    def argument_parse_annotation(self, argument, name, raw_value, location):
        """ Parse an argument annotation.  Return True if it was parsed. """

        return False

    def class_complete_definition(self, klass):
        """ Complete the definition of a class. """

        pass

    def class_get_access_specifier_keywords(self):
        """ Return a sequence of class action specifier keywords to be
        recognised by the parser.
        """

        return ()

    def class_parse_access_specifier(self, klass, primary, secondary):
        """ Parse a primary and optional secondary class access specifier.  If
        it was parsed return the C++ standard access specifier (ie. 'public',
        'protected' or 'private') to use, otherwise return None.
        """

        return None

    def class_parse_annotation(self, klass, name, raw_value, location):
        """ Parse a class annotation.  Return True if it was parsed. """

        return False

    def class_write_extension_structure(self, klass, output, structure_name):
        """ Write the code that implements a class extension data structure.
        Return True if something was written.
        """

        return False

    def ctor_parse_annotation(self, ctor, name, raw_value, location):
        """ Parse a ctor annotation.  Return True if it was parsed. """

        return False

    def dtor_parse_annotation(self, dtor, name, raw_value, location):
        """ Parse a dtor annotation.  Return True if it was parsed. """

        return False

    def enum_parse_annotation(self, enum, name, raw_value, location):
        """ Parse an enum annotation.  Return True if it was parsed. """

        return False

    def enum_member_parse_annotation(self, enum_member, name, raw_value,
            location):
        """ Parse an enum member annotation.  Return True if it was parsed. """

        return False

    def function_complete_parse(self, function, scope):
        """ Complete the parsing of a function. """

        pass

    def function_get_keywords(self):
        """ Return a sequence of function keywords to be recognised by the
        parser.
        """

        return ()

    def function_parse_annotation(self, function, name, raw_value, location):
        """ Parse a function annotation.  Return True if it was parsed. """

        return False

    def function_parse_keyword(self, function, keyword):
        """ Parse a function keyword.  Return True if it was parsed. """

        return False

    def function_group_complete_definition(self, function_group, scope):
        """ Update a function group after it has been defined. """

        pass

    def mapped_type_parse_annotation(self, mapped_type, name, raw_value,
            location):
        """ Parse a mapped type annotation.  Return True if it was parsed. """

        return False

    def mapped_type_write_extension_structure(self, mapped_type, output,
            structure_name):
        """ Write the code that implements a mapped type extension data
        structure.  Return True if something was written.
        """

        return False

    def namespace_parse_annotation(self, namespace, name, raw_value, location):
        """ Parse a namespace annotation.  Return True if it was parsed. """

        return False

    def typedef_parse_annotation(self, typedef, name, raw_value, location):
        """ Parse a typedef annotation.  Return True if it was parsed. """

        return False

    def union_parse_annotation(self, union, name, raw_value, location):
        """ Parse a union annotation.  Return True if it was parsed. """

        return False

    def variable_parse_annotation(self, variable, name, raw_value, location):
        """ Parse a variable annotation.  Return True if it was parsed. """

        return False

    def write_sip_api_h_code(self, output):
        """ Write code to be included in all generated sipAPI*.h files. """

        pass
