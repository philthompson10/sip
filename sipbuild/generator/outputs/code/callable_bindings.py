# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2024 Phil Thompson <phil@riverbankcomputing.com>


from ...python_slots import (is_hash_return_slot, is_inplace_number_slot,
        is_inplace_sequence_slot, is_int_arg_slot, is_int_return_slot,
        is_multi_arg_slot, is_number_slot, is_rich_compare_slot,
        is_ssize_return_slot, is_void_return_slot, is_zero_arg_slot)
from ...scoped_name import STRIP_GLOBAL
from ...specification import (AccessSpecifier, Argument, ArgumentType,
        ArrayArgument, GILAction, IfaceFileType, MappedType, PySlot, Transfer,
        WrappedClass, WrappedEnum)
from ...utils import get_py_scope, get_c_ref, py_as_int

from ..formatters import (fmt_argument_as_name, fmt_argument_as_cpp_type,
        fmt_enum_as_cpp_type)

from .argument_parser import argument_parser
from .docstrings import function_docstring
from .utils import (abi_supports_array, cached_name_ref, cast_zero, const_cast,
        get_enum_class_scope, get_gto_name, get_slot_name, is_string,
        is_used_in_code, scoped_class_name, type_needs_user_state,
        user_state_suffix)


def ctor_bindings(sf, spec, bindings, overloads, klass, prefix=''):
    """ Output the C function that implements the bindings for a list of
    constructor overloads.  Return a reference to the generated function.
    """

    # Handle the trivial case.
    if not overloads:
        return None

    klass_name = klass.iface_file.fq_cpp_name.as_word

    # See if we need to name the self and owner arguments so that we can avoid
    # a compiler warning about an unused argument.
    need_self = (spec.c_bindings or klass.has_shadow)
    need_owner = spec.c_bindings

    for ctor in overloads:
        if is_used_in_code(ctor.method_code, 'sipSelf'):
            need_self = True

        if ctor.transfer is Transfer.TRANSFER:
            need_owner = True
        else:
            for arg in ctor.py_signature.args:
                if not arg.is_in:
                    continue

                if arg.key is not None:
                    need_self = True

                if arg.transfer is Transfer.TRANSFER:
                    need_self = True

                if arg.transfer is Transfer.TRANSFER_THIS:
                    need_owner = True

    sf.write('\n\n')

    callable_ref = prefix + 'init_type_' + klass_name

    if not spec.c_bindings:
        sf.write(f'extern "C" {{static void *{callable_ref}(sipSimpleWrapper *, PyObject *, PyObject *, PyObject **, PyObject **, PyObject **);}}\n')

    sip_self = 'sipSelf' if need_self else ''
    sip_owner = 'sipOwner' if need_owner else ''
    sip_cpp_type = 'sip' + klass_name if klass.has_shadow else scoped_class_name(spec, klass)

    sf.write(
f'''static void *{callable_ref}(sipSimpleWrapper *{sip_self}, PyObject *sipArgs, PyObject *sipKwds, PyObject **sipUnused, PyObject **{sip_owner}, PyObject **sipParseErr)
{{
    {sip_cpp_type} *sipCpp = SIP_NULLPTR;
''')

    if bindings.tracing:
        sf.write(f'\n    sipTrace(SIP_TRACE_INITS, "{callable_ref}()\\n");\n')

    # Generate the code that parses the Python arguments and calls the correct
    # constructor.
    for ctor in overloads:
        if ctor.access_specifier is AccessSpecifier.PRIVATE:
            continue

        sf.write('\n    {\n')

        if ctor.method_code is not None:
            error_flag = needs_error_flag(ctor.method_code)
            old_error_flag = needs_old_error_flag(ctor.method_code)
        else:
            error_flag = old_error_flag = False

        argument_parser(sf, spec, klass, ctor.py_signature, ctor=ctor)
        _constructor_call(sf, spec, bindings, klass, ctor, error_flag,
                old_error_flag)

        sf.write('    }\n')

    sf.write(
'''
    return SIP_NULLPTR;
}
''')

    return callable_ref


def function_bindings(sf, spec, bindings, overloads, scope=None, prefix=''):
    """ Output the C function that implements the bindings for a list of
    function overloads with the same Python name.  Return a 2-tuple of a
    reference to the generated function (or None if there was none) and to the
    docstring.
    """

    # Handle the trivial case.
    if not overloads:
        return None, 'SIP_NULLPTR'

    if overloads[0].common.py_slot is not None:
        callable_ref = _py_slot_function(sf, spec, bindings, overloads, scope,
                prefix)

        return callable_ref, 'SIP_NULLPTR'

    if isinstance(scope, WrappedClass) and scope.iface_file.type is IfaceFileType.CLASS:
        return _member_function(sf, spec, bindings, overloads, scope, prefix)

    return _ordinary_function(sf, spec, bindings, overloads, scope, prefix)


def pyqt_emitters(sf, spec, klass):
    """ Generate the PyQt emitters for a class. """

    klass_name = klass.iface_file.fq_cpp_name.as_word
    scope_s = scoped_class_name(spec, klass)
    klass_name_ref = cached_name_ref(klass.py_name)

    for member in klass.members:
        in_emitter = False

        for overload in member.overloads:
            if not (overload.pyqt_is_signal and pyqt_has_optional_args(overload)):
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

            # Generate the code that parses the args and emits the appropriate
            # overloaded signal.
            sf.write('\n    {\n')

            argument_parser(sf, spec, klass, overload.py_signature)

            sf.write(
f'''        {{
            Py_BEGIN_ALLOW_THREADS
            sipCpp->{overload.cpp_name}(''')

            _call_args(sf, spec, overload.cpp_signature, overload.py_signature)

            sf.write(''');
            Py_END_ALLOW_THREADS

''')

            _delete_temporaries(sf, spec, overload.py_signature)

            sf.write(
'''
            return 0;
        }
    }
''')

        if in_emitter:
            member_name_ref = cached_name_ref(member.py_name)

            sf.write(
f'''
    sipNoMethod(sipParseErr, {klass_name_ref}, {member_name_ref}, SIP_NULLPTR);

    return -1;
}}
''')


def pyqt_has_optional_args(overload):
    """ Return True if an overload has optional arguments. """

    args = overload.cpp_signature.args

    return len(args) != 0 and args[-1].default_value is not None


def _constructor_call(sf, spec, bindings, klass, ctor, error_flag,
        old_error_flag):
    """ Generate a single constructor call. """

    klass_name = klass.iface_file.fq_cpp_name.as_word
    scope_s = scoped_class_name(spec, klass)

    sf.write('        {\n')

    if ctor.premethod_code is not None:
        sf.write('\n')
        sf.write_code(ctor.premethod_code)
        sf.write('\n')

    if error_flag:
        sf.write('            sipErrorState sipError = sipErrorNone;\n\n')
    elif old_error_flag:
        sf.write('            int sipIsErr = 0;\n\n')

    if ctor.deprecated:
        # Note that any temporaries will leak if an exception is raised.
        sf.write(
f'''            if (sipDeprecated({cached_name_ref(klass.py_name)}, SIP_NULLPTR) < 0)
                return SIP_NULLPTR;

''')

    # Call any pre-hook.
    if ctor.prehook is not None:
        sf.write(f'            sipCallHook("{ctor.prehook}");\n\n')

    if ctor.method_code is not None:
        sf.write_code(ctor.method_code)
    elif spec.c_bindings:
        sf.write(f'            sipCpp = sipMalloc(sizeof ({scope_s}));\n')
    else:
        release_gil = _release_gil(ctor.gil_action, bindings)

        if ctor.raises_py_exception:
            sf.write('            PyErr_Clear();\n\n')

        if release_gil:
            sf.write('            Py_BEGIN_ALLOW_THREADS\n')

        _try(sf, bindings, ctor.throw_args)

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
            _call_args(sf, spec, ctor.cpp_signature, ctor.py_signature)

        sf.write(');\n')

        _catch(sf, spec, bindings, ctor.py_signature, ctor.throw_args,
                release_gil)

        if release_gil:
            sf.write('            Py_END_ALLOW_THREADS\n')

        # This is a bit of a hack to say we want the result transferred.  We
        # don't simply call sipTransferTo() because the wrapper object hasn't
        # been fully initialised yet.
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

    _gc_ellipsis(sf, ctor.py_signature)
    _delete_temporaries(sf, spec, ctor.py_signature)

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


def _ordinary_function(sf, spec, bindings, overloads, scope, prefix):
    """ Output the C function that implements the bindings for a list of
    non-method, non-slot overloads with the same Python name.  Return a 2-tuple
    of a reference to the generated function and the docstring.
    """

    # 'scope' can be either None for global functions, MappedType for mapped
    # type functions WrappedClass for namespace functions (hidden or
    # otherwise).

    sf.write('\n\n')

    docstring_ref, errstring_ref = function_docstring(sf, spec, bindings,
            overloads, scope, prefix)

    member = overloads[0].common

    if member.no_arg_parser or member.allow_keyword_args:
        kw_fw_decl = ', PyObject *'
        kw_decl = ', PyObject *sipKwds'
    else:
        kw_fw_decl = kw_decl = ''

    sip_self_unused = False

    if get_py_scope(scope) is None:
        # Either no scope or a hidden namespace.
        callable_ref = get_c_ref('func', scope, member.py_name.name,
                prefix=prefix)

        if not spec.c_bindings:
            sf.write(f'extern "C" {{static PyObject *{callable_ref}(PyObject *, PyObject *{kw_fw_decl});}}\n')
            sip_self = ''
        else:
            sip_self = 'sipSelf'
            sip_self_unused = True;

        sf.write(f'static PyObject *{callable_ref}(PyObject *{sip_self}, PyObject *sipArgs{kw_decl})\n')
    else:
        # Either a namespace or a mapped type.
        callable_ref = get_c_ref('meth', scope, member.py_name.name,
                prefix=prefix)

        if not spec.c_bindings:
            sf.write(f'extern "C" {{static PyObject *{callable_ref}(PyObject *, PyObject *{kw_fw_decl});}}\n')

        sf.write(f'static PyObject *{callable_ref}(PyObject *, PyObject *sipArgs{kw_decl})\n')

    sf.write('{\n')

    if member.no_arg_parser:
        sf.write_code(overloads[0].method_code)
    else:
        sf.write('    PyObject *sipParseErr = SIP_NULLPTR;\n')

        if sip_self_unused:
            sf.write('\n    (void)sipSelf;\n')

        for overload in overloads:
            _function_body(sf, spec, bindings, scope, overload)

        sf.write(
f'''
    /* Raise an exception if the arguments couldn't be parsed. */
    sipNoFunction(sipParseErr, {cached_name_ref(member.py_name)}, {errstring_ref});

    return SIP_NULLPTR;
''')

    sf.write('}\n')

    return callable_ref, docstring_ref


def _member_function(sf, spec, bindings, overloads, klass, prefix):
    """ Generate a class member function.  Return a 2-tuple of a reference to
    the generated function (or None if there was none) and the docstring.
    """

    # Check that there is at least one overload from this class that needs to
    # be handled.  See if we can avoid naming the "self" argument (and suppress
    # a compiler warning).  See if we need to remember if "self" was explicitly
    # passed as an argument.  See if we need to handle keyword arguments.
    member = None
    need_self = need_args = need_selfarg = need_orig_self = False

    # This is the list of overloads for which code is generated.  It excludes
    # PyQt signals and abstract class functions.  A PyMethodDef may still be
    # needed even if this list is empty if private class functions have been
    # defined in order to hide non-private functions in a super-class.
    callable_overloads = []

    for overload in overloads:
        if overload.pyqt_is_signal:
            continue

        # Abstract private class functions don't require a PyMethodDef.
        # (Non-abstract ones do in order to hide any non-private function in a
        # super-class.)
        if overload.access_specifier is AccessSpecifier.PRIVATE:
            # Abstract private class functions don't need a PyMethodDef.
            if overload.is_abstract:
                continue
        else:
            callable_overloads.append(overload)

        member = overload.common

        if overload.access_specifier is not AccessSpecifier.PRIVATE:
            need_args = True

            if spec.abi_version >= (13, 0) or not overload.is_static:
                need_self = True

                if overload.is_abstract:
                    need_orig_self = True
                elif overload.is_virtual or overload.is_virtual_reimplementation or is_used_in_code(overload.method_code, 'sipSelfWasArg'):
                    need_selfarg = True

    # Handle the trivial case.
    if member is None:
        return None, 'SIP_NULLPTR'

    sf.write('\n\n')

    klass_name = klass.iface_file.fq_cpp_name.as_word
    member_py_name = member.py_name.name

    # Generate the docstrings.
    docstring_ref, errstring_ref = function_docstring(sf, spec, bindings,
            callable_overloads, klass, prefix)

    if member.no_arg_parser or member.allow_keyword_args:
        arg3_type = ', PyObject *'
        arg3_decl = ', PyObject *sipKwds'
    else:
        arg3_type = ''
        arg3_decl = ''

    sip_self = 'sipSelf' if need_self else ''
    sip_args = 'sipArgs' if need_args else ''

    callable_ref = get_c_ref('meth', klass, member.py_name.name, prefix=prefix)

    if not spec.c_bindings:
        sf.write(f'extern "C" {{static PyObject *{callable_ref}(PyObject *, PyObject *{arg3_type});}}\n')

    sf.write(f'static PyObject *{callable_ref}(PyObject *{sip_self}, PyObject *{sip_args}{arg3_decl})\n{{\n')

    if bindings.tracing:
        sf.write(f'    sipTrace(SIP_TRACE_METHODS, "{callable_ref}()\\n");\n\n')

    if not member.no_arg_parser:
        if need_args:
            sf.write('    PyObject *sipParseErr = SIP_NULLPTR;\n')

        if need_selfarg:
            # This determines if we call the explicitly scoped version or the
            # unscoped version (which will then go via the vtable).
            #
            # - If the call was unbound and self was passed as the first
            #   argument (ie. Foo.meth(self)) then we always want to call the
            #   explicitly scoped version.
            #
            # - If the call was bound then we only call the unscoped version in
            #   case there is a C++ sub-class reimplementation that Python
            #   knows nothing about.  Otherwise, if the call was invoked by
            #   super() within a Python reimplementation then the Python
            #   reimplementation would be called recursively.
            #
            # In addition, if the type is a derived class then we know that
            # there can't be a C++ sub-class that we don't know about so we can
            # avoid the vtable.
            #
            # Note that we would like to rename 'sipSelfWasArg' to
            # 'sipExplicitScope' but it is part of the public API.
            if spec.abi_version >= (13, 0):
                sipself_test = f'!PyObject_TypeCheck(sipSelf, sipTypeAsPyTypeObject({get_gto_name(klass)}))'
            else:
                sipself_test = '!sipSelf'

            sf.write(f'    bool sipSelfWasArg = ({sipself_test} || sipIsDerivedClass((sipSimpleWrapper *)sipSelf));\n')

        if need_orig_self:
            # This is similar to the above but for abstract methods.  We allow
            # the (potential) recursion because it means that the concrete
            # implementation can be put in a mixin and it will all work.
            sf.write('    PyObject *sipOrigSelf = sipSelf;\n')

    if member.no_arg_parser:
        sf.write_code(overload.method_code)
    else:
        for overload in callable_overloads:
            if member.no_arg_parser:
                sf.write_code(overload.method_code)
                break

            _function_body(sf, spec, bindings, klass, overload,
                    original_klass=member.scope)

        sip_parse_err = 'sipParseErr' if need_args else 'SIP_NULLPTR'
        klass_py_name_ref = cached_name_ref(klass.py_name)
        member_py_name_ref = cached_name_ref(member.py_name)

        sf.write(
f'''
    sipNoMethod({sip_parse_err}, {klass_py_name_ref}, {member_py_name_ref}, {errstring_ref});

    return SIP_NULLPTR;
''')

    sf.write('}\n')

    return callable_ref, docstring_ref


def _py_slot_function(sf, spec, bindings, overloads, scope, prefix):
    """ Generate a Python slot function for either a class, an enum or an
    extender.  Return a reference to the generated function.
    """

    if scope is None:
        py_name = None
        fq_cpp_name = None
    elif isinstance(scope, WrappedEnum):
        py_name = scope.py_name
        fq_cpp_name = scope.fq_cpp_name
    else:
        py_name = scope.py_name
        fq_cpp_name = scope.iface_file.fq_cpp_name

    member = overloads[0].common

    if is_void_return_slot(member.py_slot) or is_int_return_slot(member.py_slot):
        ret_type = 'int '
        ret_value = '-1'
    elif is_ssize_return_slot(member.py_slot):
        ret_type = 'Py_ssize_t '
        ret_value = '0'
    elif is_hash_return_slot(member.py_slot):
        if spec.abi_version >= (13, 0):
            ret_type = 'Py_hash_t '
            ret_value = '0'
        else:
            ret_type = 'long '
            ret_value = '0L'
    else:
        ret_type = 'PyObject *'
        ret_value = 'SIP_NULLPTR'

    has_args = True

    if is_int_arg_slot(member.py_slot):
        has_args = False
        arg_str = 'PyObject *sipSelf, int a0'
        decl_arg_str = 'PyObject *, int'
    elif member.py_slot is PySlot.CALL:
        if spec.c_bindings or member.allow_keyword_args or member.no_arg_parser:
            arg_str = 'PyObject *sipSelf, PyObject *sipArgs, PyObject *sipKwds'
        else:
            arg_str = 'PyObject *sipSelf, PyObject *sipArgs, PyObject *'

        decl_arg_str = 'PyObject *, PyObject *, PyObject *'
    elif is_multi_arg_slot(member.py_slot):
        arg_str = 'PyObject *sipSelf, PyObject *sipArgs'
        decl_arg_str = 'PyObject *, PyObject *'
    elif is_zero_arg_slot(member.py_slot):
        has_args = False
        arg_str = 'PyObject *sipSelf'
        decl_arg_str = 'PyObject *'
    elif is_number_slot(member.py_slot):
        arg_str = 'PyObject *sipArg0, PyObject *sipArg1'
        decl_arg_str = 'PyObject *, PyObject *'
    elif member.py_slot is PySlot.SETATTR:
        arg_str = 'PyObject *sipSelf, PyObject *sipName, PyObject *sipValue'
        decl_arg_str = 'PyObject *, PyObject *, PyObject *'
    else:
        arg_str = 'PyObject *sipSelf, PyObject *sipArg'
        decl_arg_str = 'PyObject *, PyObject *'

    sf.write('\n\n')

    callable_ref = get_c_ref('slot', scope, member.py_name.name, prefix=prefix)

    if not spec.c_bindings:
        sf.write(f'extern "C" {{static {ret_type}{callable_ref}({decl_arg_str});}}\n')

    sf.write(f'static {ret_type}{callable_ref}({arg_str})\n{{\n')

    if member.py_slot is PySlot.CALL and member.no_arg_parser:
        for overload in member.overloads:
            sf.write_code(overload.method_code)
    else:
        if is_inplace_number_slot(member.py_slot):
            sf.write(
f'''    if (!PyObject_TypeCheck(sipSelf, sipTypeAsPyTypeObject(sipType_{fq_cpp_name.as_word})))
    {{
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }}

''')

        if not is_number_slot(member.py_slot):
            as_cpp = fq_cpp_name.as_cpp
            gto_name = get_gto_name(scope)

            if isinstance(scope, WrappedClass):
                sf.write(
f'''    {as_cpp} *sipCpp = reinterpret_cast<{as_cpp} *>(sipGetCppPtr((sipSimpleWrapper *)sipSelf, {gto_name}));

    if (!sipCpp)
''')
            else:
                sf.write(
f'''    {as_cpp} sipCpp = static_cast<{as_cpp}>(sipConvertToEnum(sipSelf, {gto_name}));

    if (PyErr_Occurred())
''')

            sf.write(f'        return {ret_value};\n\n')

        if has_args:
            sf.write('    PyObject *sipParseErr = SIP_NULLPTR;\n')

        for overload in member.overloads:
            if overload.is_abstract:
                sf.write('    PyObject *sipOrigSelf = sipSelf;\n')
                break

        scope_not_enum = not isinstance(scope, WrappedEnum)

        for overload in member.overloads:
            dereferenced = scope_not_enum and not overload.dont_deref_self

            _function_body(sf, spec, bindings, scope, overload,
                    dereferenced=dereferenced)

        if has_args:
            if member.py_slot in (PySlot.CONCAT, PySlot.ICONCAT, PySlot.REPEAT, PySlot.IREPEAT):
                sf.write(
f'''
    /* Raise an exception if the argument couldn't be parsed. */
    sipBadOperatorArg(sipSelf, sipArg, {get_slot_name(member.py_slot)});

    return SIP_NULLPTR;
''')

            else:
                if is_rich_compare_slot(member.py_slot):
                    sf.write(
'''
    Py_XDECREF(sipParseErr);
''')
                elif is_number_slot(member.py_slot) or is_inplace_number_slot(member.py_slot):
                    sf.write(
'''
    Py_XDECREF(sipParseErr);

    if (sipParseErr == Py_None)
        return SIP_NULLPTR;
''')

                if is_number_slot(member.py_slot) or is_rich_compare_slot(member.py_slot):
                    # We can only extend class slots. */
                    if not isinstance(scope, WrappedClass):
                        sf.write(
'''
    PyErr_Clear();

    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
''')
                    elif is_number_slot(member.py_slot):
                        sf.write(
f'''
    return sipPySlotExtend(&sipModuleAPI_{spec.module.py_name}, {get_slot_name(member.py_slot)}, SIP_NULLPTR, sipArg0, sipArg1);
''')
                    else:
                        sf.write(
f'''
    return sipPySlotExtend(&sipModuleAPI_{spec.module.py_name}, {get_slot_name(member.py_slot)}, {get_gto_name(scope)}, sipSelf, sipArg);
''')
                elif is_inplace_number_slot(member.py_slot):
                    sf.write(
'''
    PyErr_Clear();

    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
''')
                else:
                    member_name = '(sipValue != SIP_NULLPTR ? sipName___setattr__ : sipName___delattr__)' if member.py_slot is PySlot.SETATTR else cached_name_ref(member.py_name)

                    sf.write(
f'''
    sipNoMethod(sipParseErr, {cached_name_ref(py_name)}, {member_name}, SIP_NULLPTR);

    return {ret_value};
''')
        else:
            sf.write(
'''
    return 0;
''')

    sf.write('}\n')

    return callable_ref


def _function_body(sf, spec, bindings, scope, overload, original_klass=None,
        dereferenced=True):
    """ Generate the function calls for a particular overload. """

    if scope is None:
        original_scope = None
    elif isinstance(scope, WrappedClass):
        # If there was no original class (ie. where a virtual was first
        # defined) then use this class,
        if original_klass is None:
            original_klass = scope

        original_scope = original_klass
    else:
        original_scope = scope

    py_signature = overload.py_signature

    sf.write('\n    {\n')

    # In case we have to fiddle with it.
    py_signature_adjusted = False

    if is_number_slot(overload.common.py_slot):
        # Number slots must have two arguments because we parse them slightly
        # differently.
        if len(py_signature.args) == 1:
            py_signature.args.append(py_signature.args[0])

            # Insert self in the right place.
            py_signature.args[0] = Argument(type=ArgumentType.CLASS,
                    is_in=True, is_reference=True, definition=original_klass)

            py_signature_adjusted = True

        argument_parser(sf, spec, scope, py_signature, overload=overload)
    elif not is_int_arg_slot(overload.common.py_slot) and not is_zero_arg_slot(overload.common.py_slot):
        argument_parser(sf, spec, scope, py_signature, overload=overload)

    _function_call(sf, spec, bindings, scope, overload, dereferenced,
            original_scope)

    sf.write('    }\n')

    if py_signature_adjusted:
        del overload.py_signature.args[0]


def _function_call(sf, spec, bindings, scope, overload, dereferenced,
        original_scope):
    """ Generate a function call. """

    py_slot = overload.common.py_slot
    result = overload.py_signature.result
    result_cpp_type = fmt_argument_as_cpp_type(spec, result, plain=True,
            no_derefs=True)
    static_factory = (scope is None or overload.is_static) and overload.factory

    sf.write('        {\n')

    # If there is no shadow class then protected methods can never be called.
    if overload.access_specifier is AccessSpecifier.PROTECTED and not scope.has_shadow:
        sf.write(
'''            /* Never reached. */
        }
''')

        return

    # Save the full result type as we may want to fiddle with it.
    saved_result_is_const = result.is_const

    # See if we need to make a copy of the result on the heap.
    is_new_instance = needs_heap_copy(result, using_copy_ctor=False)

    if is_new_instance:
        result.is_const = False

    result_decl = _get_result_decl(spec, scope, overload, result)
    if result_decl is not None:
        sf.write('            ' + result_decl + ';\n')
        separating_newline = True
    else:
        separating_newline = False

    # See if we want to keep a reference to the result.
    post_process = result.key is not None

    delete_temporaries = True
    result_size_arg_nr = -1

    for arg_nr, arg in enumerate(overload.py_signature.args):
        if arg.result_size:
            result_size_arg_nr = arg_nr

        if static_factory and arg.key is not None:
            post_process = True

        # If we have an In,Out argument that has conversion code then we delay
        # the destruction of any temporary variables until after we have
        # converted the outputs.
        if arg.is_in and arg.is_out and _get_convert_to_type_code(arg) is not None:
            delete_temporaries = False
            post_process = True

        # If we are returning a class via an output only reference or pointer
        # then we need an instance on the heap.
        if arg.type in (ArgumentType.CLASS, ArgumentType.MAPPED) and _needs_new_instance(arg):
            arg_name = fmt_argument_as_name(spec, arg, arg_nr)
            arg_cpp_type = fmt_argument_as_cpp_type(spec, arg, plain=True,
                    no_derefs=True)
            sf.write(f'            {arg_name} = new {arg_cpp_type}();\n')
            separating_newline = True

    if post_process:
        sf.write('            PyObject *sipResObj;\n')
        separating_newline = True

    if overload.premethod_code is not None:
        sf.write('\n')
        sf.write_code(overload.premethod_code)

    error_flag = old_error_flag = False

    if overload.method_code is not None:
        # See if the handwritten code seems to be using the error flag.
        if needs_error_flag(overload.method_code):
            sf.write('            sipErrorState sipError = sipErrorNone;\n')
            error_flag = True
            separating_newline = True
        elif needs_old_error_flag(overload.method_code):
            sf.write('            int sipIsErr = 0;\n')
            old_error_flag = True
            separating_newline = True

    if separating_newline:
        sf.write('\n')

    # If it is abstract make sure that self was bound.
    if overload.is_abstract:
        sf.write(
f'''            if (!sipOrigSelf)
            {{
                sipAbstractMethod({cached_name_ref(scope.py_name)}, {cached_name_ref(overload.common.py_name)});
                return SIP_NULLPTR;
            }}

''')

    if overload.deprecated:
        scope_py_name_ref = cached_name_ref(scope.py_name) if scope is not None and scope.py_name is not None else 'SIP_NULLPTR'
        error_return = '-1' if is_void_return_slot(py_slot) or is_int_return_slot(py_slot) or is_ssize_return_slot(py_slot) or is_hash_return_slot(py_slot) else 'SIP_NULLPTR'

        # Note that any temporaries will leak if an exception is raised.
        sf.write(
f'''            if (sipDeprecated({scope_py_name_ref}, {cached_name_ref(overload.common.py_name)}) < 0)
                return {error_return};

''')

    # Call any pre-hook.
    if overload.prehook is not None:
        sf.write(f'            sipCallHook("{overload.prehook}");\n\n')

    if overload.method_code is not None:
        sf.write_code(overload.method_code)
    else:
        release_gil = _release_gil(overload.gil_action, bindings)
        needs_closing_paren = False

        if is_new_instance and spec.c_bindings:
            sf.write(
f'''            if ((sipRes = ({result_cpp_type} *)sipMalloc(sizeof ({result_cpp_type}))) == SIP_NULLPTR)
        {{
''')

            _gc_ellipsis(sf, overload.py_signature)

            sf.write(
'''                return SIP_NULLPTR;
            }

''')

        if overload.raises_py_exception:
            sf.write('            PyErr_Clear();\n\n')

        if isinstance(scope, WrappedClass) and scope.len_cpp_name is not None:
            _sequence_support(sf, spec, scope, overload)

        if release_gil:
            sf.write('            Py_BEGIN_ALLOW_THREADS\n')

        _try(sf, bindings, overload.throw_args)

        sf.write('            ')

        if result_decl is not None:
            # Construct a copy on the heap if needed.
            if is_new_instance:
                if spec.c_bindings:
                    sf.write('*sipRes = ')
                elif result.type is ArgumentType.CLASS and result.definition.cannot_copy:
                    sf.write(f'sipRes = reinterpret_cast<{result_cpp_type} *>(::operator new(sizeof ({result_cpp_type})));\n            *sipRes = ')
                else:
                    sf.write(f'sipRes = new {result_cpp_type}(')
                    needs_closing_paren = True
            else:
                sf.write('sipRes = ')

                # See if we need the address of the result.  Any reference will
                # be non-const.
                if result.type in (ArgumentType.CLASS, ArgumentType.MAPPED) and (len(result.derefs) == 0 or result.is_reference):
                    sf.write('&')

        if py_slot is None:
            _cpp_function_call(sf, spec, scope, overload, original_scope)
        elif py_slot is PySlot.CALL:
            sf.write('(*sipCpp)(')
            _call_args(sf, spec, overload.cpp_signature, overload.py_signature)
            sf.write(')')
        else:
            sf.write(_get_slot_call(spec, scope, overload, dereferenced))

        if needs_closing_paren:
            sf.write(')')

        sf.write(';\n')

        _catch(sf, spec, bindings, overload.py_signature, overload.throw_args,
                release_gil)

        if release_gil:
            sf.write('            Py_END_ALLOW_THREADS\n')

    for arg_nr, arg in enumerate(overload.py_signature.args):
        if not arg.is_in:
            continue

        # Handle any /KeepReference/ arguments except for static factories.
        if not static_factory and arg.key is not None:
            sip_self = 'SIP_NULLPTR' if scope is None or overload.is_static else 'sipSelf'
            keep_reference_call = _get_keep_reference_call(spec, arg, arg_nr,
                    sip_self)

            sf.write(f'\n            {keep_reference_call};\n')

        # Handle /TransferThis/ for non-factory methods.
        if not overload.factory and arg.transfer is Transfer.TRANSFER_THIS:
            sf.write(
'''
            if (sipOwner)
                sipTransferTo(sipSelf, (PyObject *)sipOwner);
            else
                sipTransferBack(sipSelf);
''')

    if overload.transfer is Transfer.TRANSFER_THIS:
        sf.write('\n            sipTransferTo(sipSelf, SIP_NULLPTR);\n')

    _gc_ellipsis(sf, overload.py_signature)

    if delete_temporaries and not is_zero_arg_slot(py_slot):
        _delete_temporaries(sf, spec, overload.py_signature)

    sf.write('\n')

    # Handle the error flag if it was used.
    error_value = '-1' if is_void_return_slot(py_slot) or is_int_return_slot(py_slot) or is_ssize_return_slot(py_slot) or is_hash_return_slot(py_slot) else '0'

    if overload.raises_py_exception:
        sf.write(
f'''            if (PyErr_Occurred())
                return {error_value};

''')
    elif error_flag:
        if not is_zero_arg_slot(py_slot):
            sf.write(
f'''            if (sipError == sipErrorFail)
                return {error_value};

''')

        sf.write(
'''            if (sipError == sipErrorNone)
            {
''')
    elif old_error_flag:
        sf.write(
f'''            if (sipIsErr)
                return {error_value};

''')

    # Call any post-hook.
    if overload.posthook is not None:
        sf.write(f'\n            sipCallHook("{overload.posthook}");\n')

    if is_void_return_slot(py_slot):
        sf.write(
'''            return 0;
''')
    elif is_inplace_number_slot(py_slot) or is_inplace_sequence_slot(py_slot):
        sf.write(
'''            Py_INCREF(sipSelf);
            return sipSelf;
''')
    elif is_int_return_slot(py_slot) or is_ssize_return_slot(py_slot) or is_hash_return_slot(py_slot):
        sf.write(
'''            return sipRes;
''')
    else:
        action = 'sipResObj =' if post_process else 'return'
        _handle_result(sf, spec, overload, is_new_instance, result_size_arg_nr,
                action)

        # Delete the temporaries now if we haven't already done so.
        if not delete_temporaries:
            _delete_temporaries(sf, spec, overload.py_signature)

        # Keep a reference to a pointer to a class if it isn't owned by Python.
        if result.key is not None:
            sip_self = 'SIP_NULLPTR' if overload.is_static else 'sipSelf'
            sf.write(f'\n            sipKeepReference({sip_self}, {result.key}, sipResObj);\n')

        # Keep a reference to any argument with the result if the function is a
        # static factory.
        if static_factory:
            for arg_nr, arg in enumerate(overload.py_signature.args):
                if not arg.is_in:
                    continue

                if arg.key != None:
                    keep_reference_call = _get_keep_reference_call(spec, arg,
                            arg_nr, 'sipResObj')
                    sf.write(f'\n            {keep_reference_call};\n')

        if post_process:
            sf.write('\n            return sipResObj;\n')

    if error_flag:
        sf.write('            }\n')

        if not is_zero_arg_slot(py_slot):
            sf.write('\n            sipAddException(sipError, &sipParseErr);\n')

    sf.write('        }\n')

    # Restore the full state of the result.
    result.is_const = saved_result_is_const


def _cpp_function_call(sf, spec, scope, overload, original_scope):
    """ Generate a call to a C++ function. """

    cpp_name = overload.cpp_name

    # If the function is protected then call the public wrapper.  If it is
    # virtual then call the explicit scoped function if "self" was passed as
    # the first argument.

    nr_parens = 1

    if scope is None:
        sf.write(cpp_name + '(')
    elif scope.iface_file.type is IfaceFileType.NAMESPACE:
        sf.write(f'{scope.iface_file.fq_cpp_name.as_cpp}::{cpp_name}(')
    elif overload.is_static:
        if overload.access_specifier is AccessSpecifier.PROTECTED:
            sf.write(f'sip{scope.iface_file.fq_cpp_name.as_word}::sipProtect_{cpp_name}(')
        else:
            sf.write(f'{original_scope.iface_file.fq_cpp_name.as_cpp}::{cpp_name}(')
    elif overload.access_specifier is AccessSpecifier.PROTECTED:
        if not overload.is_abstract and (overload.is_virtual or overload.is_virtual_reimplementation):
            sf.write(f'sipCpp->sipProtectVirt_{cpp_name}(sipSelfWasArg')

            if len(overload.cpp_signature.args) != 0:
                sf.write(', ')
        else:
            sf.write(f'sipCpp->sipProtect_{cpp_name}(')
    elif not overload.is_abstract and (overload.is_virtual or overload.is_virtual_reimplementation):
        sf.write(f'(sipSelfWasArg ? sipCpp->{original_scope.iface_file.fq_cpp_name.as_cpp}::{cpp_name}(')
        _call_args(sf, spec, overload.cpp_signature, overload.py_signature)
        sf.write(f') : sipCpp->{cpp_name}(')
        nr_parens += 1
    else:
        sf.write(f'sipCpp->{cpp_name}(')

    _call_args(sf, spec, overload.cpp_signature, overload.py_signature)

    sf.write(')' * nr_parens)


def _call_args(sf, spec, cpp_signature, py_signature):
    """ Generate typed arguments for a call. """

    for arg_nr, arg in enumerate(cpp_signature.args):
        if arg_nr > 0:
            sf.write(', ')

        # See if the argument needs dereferencing or it's address taking.
        indirection = ''
        nr_derefs = len(arg.derefs)

        if arg.type in (ArgumentType.ASCII_STRING, ArgumentType.LATIN1_STRING, ArgumentType.UTF8_STRING, ArgumentType.SSTRING, ArgumentType.USTRING, ArgumentType.STRING, ArgumentType.WSTRING):
            if nr_derefs > (0 if arg.is_out else 1) and not arg.is_reference:
                indirection = '&'

        elif arg.type in (ArgumentType.CLASS, ArgumentType.MAPPED):
            if nr_derefs == 2:
                indirection = '&'
            elif nr_derefs == 0:
                indirection = '*'

        elif arg.type in (ArgumentType.STRUCT, ArgumentType.UNION, ArgumentType.VOID):
            if nr_derefs == 2:
                indirection = '&'

        else:
            if nr_derefs == 1:
                indirection = '&'

        # See if we need to cast a Python void * to the correct C/C++ pointer
        # type.  Note that we assume that the arguments correspond and are just
        # different types.
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
            sf.write(indirection)

            if arg.array is ArrayArgument.ARRAY_SIZE:
                sf.write(f'({arg_cpp_type_name})')

            sf.write(arg_name)


def _delete_outs(sf, spec, py_signature):
    """ Generate the code to delete any instances created to hold /Out/
    arguments.
    """

    for arg_nr, arg in enumerate(py_signature.args):
        if arg.type in (ArgumentType.CLASS, ArgumentType.MAPPED) and _needs_new_instance(arg):
            sf.write(f'                delete {fmt_argument_as_name(spec, arg, arg_nr)};\n')


def _delete_temporaries(sf, spec, py_signature):
    """ Generate the code to delete any temporary variables on the heap created
    by type convertors.
    """

    for arg_nr, arg in enumerate(py_signature.args):
        arg_name = fmt_argument_as_name(spec, arg, arg_nr)

        if arg.array is ArrayArgument.ARRAY and arg.type in (ArgumentType.CLASS, ArgumentType.MAPPED):
            if arg.transfer is not Transfer.TRANSFER:
                extra_indent = ''

                if arg.type is ArgumentType.CLASS and abi_supports_array(spec):
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
            convert_to_type_code = _get_convert_to_type_code(arg)

            if convert_to_type_code is not None:
                if arg.type is ArgumentType.MAPPED and arg.definition.no_release:
                    continue

                sf.write(f'            sipReleaseType{user_state_suffix(spec, arg)}(')

                if spec.c_bindings or not arg.is_const:
                    sf.write(arg_name)
                else:
                    arg_cpp_plain = fmt_argument_as_cpp_type(spec, arg,
                            plain=True, no_derefs=True)
                    sf.write(f'const_cast<{arg_cpp_plain} *>({arg_name})')

                sf.write(f', {get_gto_name(arg.definition)}, {arg_name}State')

                if type_needs_user_state(arg):
                    sf.write(f', {arg_name}UserState')

                sf.write(');\n')


def _gc_ellipsis(sf, signature):
    """ Generate the code to garbage collect any ellipsis argument. """

    last = len(signature.args) - 1

    if last >= 0 and signature.args[last].type is ArgumentType.ELLIPSIS:
        sf.write(f'\n            Py_DECREF(a{last});\n')


def _get_convert_to_type_code(type):
    """ Return a type's %ConvertToTypeCode. """

    if type.type in (ArgumentType.CLASS, ArgumentType.MAPPED) and not type.is_constrained:
        return type.definition.convert_to_type_code

    return None


def _get_keep_reference_call(spec, arg, arg_nr, object_name):
    """ Return a call to sipKeepReference() for an argument. """

    arg_name = fmt_argument_as_name(spec, arg, arg_nr)
    suffix = 'Wrapper' if arg.get_wrapper and (arg.type not in (ArgumentType.ASCII_STRING, ArgumentType.LATIN1_STRING, ArgumentType.UTF8_STRING) or len(arg.derefs) != 1) else 'Keep'

    return f'sipKeepReference({object_name}, {arg.key}, {arg_name}{suffix})'


def _get_named_value_decl(spec, scope, type, name):
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

    named_value_decl = fmt_argument_as_cpp_type(spec, type, name=name,
            scope=scope.iface_file if isinstance(scope, (WrappedClass, MappedType)) else None)

    type.derefs = saved_derefs
    type.is_const = saved_is_const
    type.is_reference = saved_is_reference

    return named_value_decl


def _get_slot_arg(spec, overload, arg_nr):
    """ Return an argument to a slot call. """

    arg = overload.py_signature.args[arg_nr]

    dereference = '*' if arg.type in (ArgumentType.CLASS, ArgumentType.MAPPED) and len(arg.derefs) == 0 else ''

    return dereference + fmt_argument_as_name(spec, arg, arg_nr)


def _get_slot_call(spec, scope, overload, dereferenced):
    """ Return the call to a Python slot (except for PySlot.CALL which is
    handled separately).
    """

    py_slot = overload.common.py_slot

    if py_slot is PySlot.GETITEM:
        return f'(*sipCpp)[{_get_slot_arg(spec, overload, 0)}]'

    if py_slot in (PySlot.INT, PySlot.FLOAT):
        return '*sipCpp'

    if py_slot is PySlot.ADD:
        return _get_number_slot_call(spec, overload, '+')

    if py_slot is PySlot.CONCAT:
        return _get_binary_slot_call(spec, scope, overload, '+', dereferenced)

    if py_slot is PySlot.SUB:
        return _get_number_slot_call(spec, overload, '-')

    if py_slot in (PySlot.MUL, PySlot.MATMUL):
        return _get_number_slot_call(spec, overload, '*')

    if py_slot is PySlot.REPEAT:
        return _get_binary_slot_call(spec, scope, overload, '*', dereferenced)

    if py_slot is PySlot.TRUEDIV:
        return _get_number_slot_call(spec, overload, '/')

    if py_slot is PySlot.MOD:
        return _get_number_slot_call(spec, overload, '%')

    if py_slot is PySlot.AND:
        return _get_number_slot_call(spec, overload, '&')

    if py_slot is PySlot.OR:
        return _get_number_slot_call(spec, overload, '|')

    if py_slot is PySlot.XOR:
        return _get_number_slot_call(spec, overload, '^')

    if py_slot is PySlot.LSHIFT:
        return _get_number_slot_call(spec, overload, '<<')

    if py_slot is PySlot.RSHIFT:
        return _get_number_slot_call(spec, overload, '>>')

    if py_slot in (PySlot.IADD, PySlot.ICONCAT):
        return _get_binary_slot_call(spec, scope, overload, '+=', dereferenced)

    if py_slot is PySlot.ISUB:
        return _get_binary_slot_call(spec, scope, overload, '-=', dereferenced)

    if py_slot in (PySlot.IMUL, PySlot.IREPEAT, PySlot.IMATMUL):
        return _get_binary_slot_call(spec, scope, overload, '*=', dereferenced)

    if py_slot is PySlot.ITRUEDIV:
        return _get_binary_slot_call(spec, scope, overload, '/=', dereferenced)

    if py_slot is PySlot.IMOD:
        return _get_binary_slot_call(spec, scope, overload, '%=', dereferenced)

    if py_slot is PySlot.IAND:
        return _get_binary_slot_call(spec, scope, overload, '&=', dereferenced)

    if py_slot is PySlot.IOR:
        return _get_binary_slot_call(spec, scope, overload, '|=', dereferenced)

    if py_slot is PySlot.IXOR:
        return _get_binary_slot_call(spec, scope, overload, '^=', dereferenced)

    if py_slot is PySlot.ILSHIFT:
        return _get_binary_slot_call(spec, scope, overload, '<<=', dereferenced)

    if py_slot is PySlot.IRSHIFT:
        return _get_binary_slot_call(spec, scope, overload, '>>=', dereferenced)

    if py_slot is PySlot.INVERT:
        return '~(*sipCpp)'

    if py_slot is PySlot.LT:
        return _get_binary_slot_call(spec, scope, overload, '<', dereferenced)

    if py_slot is PySlot.LE:
        return _get_binary_slot_call(spec, scope, overload, '<=', dereferenced)

    if py_slot is PySlot.EQ:
        return _get_binary_slot_call(spec, scope, overload, '==', dereferenced)

    if py_slot is PySlot.NE:
        return _get_binary_slot_call(spec, scope, overload, '!=', dereferenced)

    if py_slot is PySlot.GT:
        return _get_binary_slot_call(spec, scope, overload, '>', dereferenced)

    if py_slot is PySlot.GE:
        return _get_binary_slot_call(spec, scope, overload, '>=', dereferenced)

    if py_slot is PySlot.NEG:
        return '-(*sipCpp)'

    if py_slot is PySlot.POS:
        return '+(*sipCpp)'

    # We should never get here.
    return ''


def _get_void_ptr_cast(type):
    """ Return a void* cast for an argument if needed. """

    # Generate a cast if the argument's type was a typedef.  This allows us to
    # use typedef's to void* to hide something more complex that we don't
    # handle.
    if type.original_typedef is None:
        return ''

    return '(const void *)' if type.is_const else '(void *)'


# A map of operators and their complements.
_OPERATOR_COMPLEMENTS = {
    '<': '>=',
    '<=': '>',
    '==': '!=',
    '!=': '==',
    '>': '<=',
    '>=': '<',
}

def _get_binary_slot_call(spec, scope, overload, operator, dereferenced):
    """ Return the call to a binary (non-number) slot method. """

    slot_call = ''

    if overload.is_complementary:
        operator = _OPERATOR_COMPLEMENTS[operator]
        slot_call += '!'

    if overload.is_global:
        # If it has been moved from a namespace then get the C++ scope.
        if overload.common.namespace_iface_file is not None:
            slot_call += overload.common.namespace_iface_file.fq_cpp_name.as_cpp + '::'

        if dereferenced:
            slot_call += f'operator{operator}((*sipCpp), '
        else:
            slot_call += f'operator{operator}(sipCpp, '
    else:
        dereference = '->' if dereferenced else '.'

        if overload.is_abstract:
            slot_call += f'sipCpp{dereference}operator{operator}('
        else:
            slot_call += f'sipCpp{dereference}{scope.iface_file.fq_cpp_name.as_cpp}::operator{operator}('

    slot_call += _get_slot_arg(spec, overload, 0)
    slot_call += ')'

    return slot_call


def _get_number_slot_call(spec, overload, operator):
    """ Return the call to a binary number slot method. """

    arg0 = _get_slot_arg(spec, overload, 0)
    arg1 = _get_slot_arg(spec, overload, 1)

    return f'({arg0} {operator} {arg1})'


def _get_result_decl(spec, scope, overload, result):
    """ Return the declaration of a variable to hold the result of a function
    call if one is needed.
    """

    # See if sipRes is needed.
    no_result = (is_inplace_number_slot(overload.common.py_slot) or
             is_inplace_sequence_slot(overload.common.py_slot) or
             (result.type is ArgumentType.VOID and len(result.derefs) == 0))

    if no_result:
        return None

    result_decl = _get_named_value_decl(spec, scope, result, 'sipRes')

    # The typical %MethodCode usually causes a compiler warning, so we
    # initialise the result in that case to try and suppress it.
    initial_value = ' = ' + cast_zero(spec, result) if overload.method_code is not None else ''

    return result_decl + initial_value


def _cast_zero(spec, arg):
    """ Return a cast to zero. """

    if arg.type is ArgumentType.ENUM:
        enum = arg.definition
        enum_type = fmt_enum_as_cpp_type(enum)

        if len(enum.members) == 0:
            return f'({enum_type})0'

        if enum.is_scoped:
            scope = enum_type
        elif enum.scope is not None:
            scope = get_enum_class_scope(spec, enum)
        else:
            scope = ''

        return scope + '::' + enum.members[0].cpp_name

    if arg.type in (ArgumentType.PYOBJECT, ArgumentType.PYTUPLE, ArgumentType.PYLIST, ArgumentType.PYDICT, ArgumentType.PYCALLABLE, ArgumentType.PYSLICE, ArgumentType.PYTYPE, ArgumentType.PYBUFFER, ArgumentType.PYENUM, ArgumentType.ELLIPSIS):
        return 'SIP_NULLPTR'

    return '0'


def _handle_result(sf, spec, overload, is_new_instance, result_size_arg_nr,
        action):
    """ Generate the code to handle the result of a call to a member function.
    """

    result = overload.py_signature.result

    if result.type is ArgumentType.VOID and len(result.derefs) == 0:
        result = None

    # See if we are returning 0, 1 or more values.
    nr_return_values = 0

    if result is not None:
        only_out_arg_nr = -1
        nr_return_values += 1

    has_owner = False

    for arg_nr, arg in enumerate(overload.py_signature.args):
        if arg.is_out:
            only_out_arg_nr = arg_nr
            nr_return_values += 1

        if arg.transfer is Transfer.TRANSFER_THIS:
            has_owner = True

    # Handle the trivial case.
    if nr_return_values == 0:
        sf.write(
f'''            Py_INCREF(Py_None);
            {action} Py_None;
''')

        return

    # Handle results that are classes or mapped types separately.
    if result is not None and result.type in (ArgumentType.CLASS, ArgumentType.MAPPED):
        result_gto_name = get_gto_name(result.definition)

        if overload.transfer is Transfer.TRANSFER_BACK:
            result_owner = 'Py_None'
        elif overload.transfer is Transfer.TRANSFER:
            result_owner = 'sipSelf'
        else:
            result_owner = 'SIP_NULLPTR'

        sip_res = const_cast(spec, result, 'sipRes')

        if is_new_instance or overload.factory:
            this_action = action if nr_return_values == 1 else 'PyObject *sipResObj ='
            owner = '(PyObject *)sipOwner' if has_owner and overload.factory else result_owner

            sf.write(f'            {this_action} sipConvertFromNewType({sip_res}, {result_gto_name}, {owner});\n')

            # Shortcut if this is the only value returned.
            if nr_return_values == 1:
                return
        else:
            need_xfer = overload.transfer is Transfer.TRANSFER and overload.is_static

            this_action = 'PyObject *sipResObj =' if nr_return_values > 1 or need_xfer else action
            owner = 'SIP_NULLPTR' if need_xfer else result_owner

            sf.write(f'            {this_action} sipConvertFromType({sip_res}, {result_gto_name}, {owner});\n')

            # Transferring the result of a static overload needs an explicit
            # call to sipTransferTo().
            if need_xfer:
                sf.write('\n           sipTransferTo(sipResObj, Py_None);\n')

            # Shortcut if this is the only value returned.
            if nr_return_values == 1:
                if need_xfer:
                    sf.write('\n           return sipResObj;\n')

                return

    # If there are multiple values then build a tuple.
    if nr_return_values > 1:
        build_result_args = ['0']

        # Build the format string.
        format_s = ''

        if result is not None:
            format_s += 'R' if result.type in (ArgumentType.CLASS, ArgumentType.MAPPED) else _get_build_result_format(result)

        for arg in overload.py_signature.args:
            if arg.is_out:
                format_s += _get_build_result_format(arg)

        build_result_args.append('"(' + format_s + ')"')

        # Pass the values for conversion.
        if result is not None:
            build_result_args.append('sipResObj' if result.type in (ArgumentType.CLASS, ArgumentType.MAPPED) else 'sipRes')

            if result.type is ArgumentType.ENUM and result.definition.fq_cpp_name is not None:
                build_result_args.append(get_gto_name(result.definition))

        for arg_nr, arg in enumerate(overload.py_signature.args):
            if arg.is_out:
                build_result_args.append(fmt_argument_as_name(spec, arg,
                        arg_nr))

                if arg.type in (ArgumentType.CLASS, ArgumentType.MAPPED):
                    build_result_args.append(get_gto_name(arg.definition))

                    transfer = 'Py_None' if arg.transfer is Transfer.TRANSFER_BACK else 'SIP_NULLPTR'
                    build_result_args.append(transfer)
                elif arg.type is ArgumentType.ENUM and arg.definition.fq_cpp_name is not None:
                    build_result_args.append(get_gto_name(arg.definition))

        build_result_args = ', '.join(build_result_args)

        sf.write(f'            {action} sipBuildResult({build_result_args});\n')

        # All done for multiple values.
        return

    # Deal with the only returned value.
    if only_out_arg_nr < 0:
        value = result
        value_name = 'sipRes'
    else:
        value = overload.py_signature.args[only_out_arg_nr]
        value_name = fmt_argument_as_name(spec, value, only_out_arg_nr)

    if value.type in (ArgumentType.CLASS, ArgumentType.MAPPED):
        need_new_instance = _needs_new_instance(value)

        convertor = 'sipConvertFromNewType' if need_new_instance else 'sipConvertFromType'
        value_name = const_cast(spec, value, value_name)
        transfer = 'Py_None' if not need_new_instance and value.transfer is Transfer.TRANSFER_BACK else 'SIP_NULLPTR'

        sf.write(f'            {action} {convertor}({value_name}, {get_gto_name(value.definition)}, {transfer});\n')

    elif value.type is ArgumentType.ENUM:
        if value.definition.fq_cpp_name is not None:
            if not spec.c_bindings:
                value_name = f'static_cast<int>({value_name})'

            sf.write(f'            {action} sipConvertFromEnum({value_name}, {get_gto_name(value.definition)});\n')
        else:
            sf.write(f'            {action} PyLong_FromLong({value_name});\n')

    elif value.type is ArgumentType.ASCII_STRING:
        if len(value.derefs) == 0:
            sf.write(f'            {action} PyUnicode_DecodeASCII(&{value_name}, 1, SIP_NULLPTR);\n')
        else:
            sf.write(
f'''            if ({value_name} == SIP_NULLPTR)
            {{
                Py_INCREF(Py_None);
                return Py_None;
            }}

            {action} PyUnicode_DecodeASCII({value_name}, strlen({value_name}), SIP_NULLPTR);
''')

    elif value.type is ArgumentType.LATIN1_STRING:
        if len(value.derefs) == 0:
            sf.write(f'            {action} PyUnicode_DecodeLatin1(&{value_name}, 1, SIP_NULLPTR);\n')
        else:
            sf.write(
f'''            if ({value_name} == SIP_NULLPTR)
            {{
                Py_INCREF(Py_None);
                return Py_None;
            }}

            {action} PyUnicode_DecodeLatin1({value_name}, strlen({value_name}), SIP_NULLPTR);
''')

    elif value.type is ArgumentType.UTF8_STRING:
        if len(value.derefs) == 0:
            sf.write(f'            {action} PyUnicode_FromStringAndSize(&{value_name}, 1);\n')
        else:
            sf.write(
f'''            if ({value_name} == SIP_NULLPTR)
            {{
                Py_INCREF(Py_None);
                return Py_None;
            }}

            {action} PyUnicode_FromString({value_name});
''')

    elif value.type in (ArgumentType.SSTRING, ArgumentType.USTRING, ArgumentType.STRING):
        cast = '' if value.type is ArgumentType.STRING else '(char *)'

        if len(value.derefs) == 0:
            sf.write(f'            {action} PyBytes_FromStringAndSize({cast}&{value_name}, 1);\n')
        else:
            sf.write(
f'''            if ({value_name} == SIP_NULLPTR)
            {{
                Py_INCREF(Py_None);
                return Py_None;
            }}

            {action} PyBytes_FromString({cast}{value_name});
''')

    elif value.type is ArgumentType.WSTRING:
        if len(value.derefs) == 0:
            sf.write(f'            {action} PyUnicode_FromWideChar(&{value_name}, 1);\n')
        else:
            sf.write(
f'''            if ({value_name} == SIP_NULLPTR)
            {{
                Py_INCREF(Py_None);
                return Py_None;
            }}

            {action} PyUnicode_FromWideChar({value_name}, (Py_ssize_t)wcslen({value_name}));
''')

    elif value.type in (ArgumentType.BOOL, ArgumentType.CBOOL):
        sf.write(f'            {action} PyBool_FromLong({value_name});\n')

    elif value.type in (ArgumentType.BYTE, ArgumentType.SBYTE, ArgumentType.SHORT, ArgumentType.INT, ArgumentType.CINT, ArgumentType.LONG):
        sf.write(f'            {action} PyLong_FromLong({value_name});\n')

    elif value.type in (ArgumentType.UBYTE, ArgumentType.USHORT, ArgumentType.UINT, ArgumentType.ULONG, ArgumentType.SIZE):
        sf.write(f'            {action} PyLong_FromUnsignedLong({value_name});\n')

    elif value.type is ArgumentType.LONGLONG:
        sf.write(f'            {action} PyLong_FromLongLong({value_name});\n')

    elif value.type is ArgumentType.ULONGLONG:
        sf.write(f'            {action} PyLong_FromUnsignedLongLong({value_name});\n')

    elif value.type is ArgumentType.SSIZE:
        sf.write(f'            {action} PyLong_FromSsize_t({value_name});\n')

    elif value.type is ArgumentType.VOID:
        convertor = 'sipConvertFromConstVoidPtr' if value.is_const else 'sipConvertFromVoidPtr'
        if result_size_arg_nr >= 0:
            convertor += 'AndSize'

        sf.write(f'            {action} {convertor}({_get_void_ptr_cast(value)}{value_name}')

        if result_size_arg_nr >= 0:
            sf.write(', ' + fmt_argument_as_name(spec, overload.py_signature.args[result_size_arg_nr], result_size_arg_nr))

        sf.write(');\n')

    elif value.type is ArgumentType.CAPSULE:
        sf.write(f'            {action} PyCapsule_New({value_name}, "{value.definition.as_cpp}", SIP_NULLPTR);\n')

    elif value.type in (ArgumentType.STRUCT, ArgumentType.UNION):
        convertor = 'sipConvertFromConstVoidPtr' if value.is_const else 'sipConvertFromVoidPtr'

        sf.write(f'            {action} {convertor}({value_name});\n')

    elif value.type in (ArgumentType.FLOAT, ArgumentType.CFLOAT):
        sf.write(f'            {action} PyFloat_FromDouble((double){value_name});\n')

    elif value.type in (ArgumentType.DOUBLE, ArgumentType.CDOUBLE):
        sf.write(f'            {action} PyFloat_FromDouble({value_name});\n')

    elif value.type in (ArgumentType.PYOBJECT, ArgumentType.PYTUPLE, ArgumentType.PYLIST, ArgumentType.PYDICT, ArgumentType.PYCALLABLE, ArgumentType.PYSLICE, ArgumentType.PYTYPE, ArgumentType.PYBUFFER, ArgumentType.PYENUM):
        sf.write(f'            {action} {value_name};\n')


def _get_build_result_format(type):
    """ Return the format string used by sipBuildResult() for a particular
    type.
    """

    if type.type in (ArgumentType.CLASS, ArgumentType.MAPPED):
        return 'N' if _needs_new_instance(type) else 'D'

    if type.type is ArgumentType.FAKE_VOID:
        return 'D'

    if type.type in (ArgumentType.BOOL, ArgumentType.CBOOL):
        return 'b'

    if type.type in (ArgumentType.ASCII_STRING, ArgumentType.LATIN1_STRING, ArgumentType.UTF8_STRING):
        return 'A' if is_string(type) else 'a'

    if type.type in (ArgumentType.SSTRING, ArgumentType.USTRING, ArgumentType.STRING):
        return 's' if is_string(type) else 'c'

    if type.type is ArgumentType.WSTRING:
        return 'x' if is_string(type) else 'w'

    if type.type is ArgumentType.ENUM:
        return 'F' if type.definition.fq_cpp_name is not None else 'e'

    if type.type in (ArgumentType.BYTE, ArgumentType.SBYTE):
        return 'L'

    if type.type is ArgumentType.UBYTE:
        return 'M'

    if type.type is ArgumentType.SHORT:
        return 'h'

    if type.type is ArgumentType.USHORT:
        return 't'

    if type.type in (ArgumentType.INT, ArgumentType.CINT):
        return 'i'

    if type.type is ArgumentType.UINT:
        return 'u'

    if type.type is ArgumentType.SIZE:
        return '='

    if type.type is ArgumentType.LONG:
        return 'l'

    if type.type is ArgumentType.ULONG:
        return 'm'

    if type.type is ArgumentType.LONGLONG:
        return 'n'

    if type.type is ArgumentType.ULONGLONG:
        return 'o'

    if type.type in (ArgumentType.STRUCT, ArgumentType.UNION, ArgumentType.VOID):
        return 'V'

    if type.type is ArgumentType.CAPSULE:
        return 'z'

    if type.type in (ArgumentType.FLOAT, ArgumentType.CFLOAT):
        return 'f'

    if type.type in (ArgumentType.DOUBLE, ArgumentType.CDOUBLE):
        return 'd'

    if type.type in (ArgumentType.PYOBJECT, ArgumentType.PYTUPLE, ArgumentType.PYLIST, ArgumentType.PYDICT, ArgumentType.PYCALLABLE, ArgumentType.PYSLICE, ArgumentType.PYTYPE, ArgumentType.PYBUFFER, ArgumentType.PYENUM):
        return 'R'

    # We should never get here.
    return ''


def needs_error_flag(code):
    """ Return True if handwritten code uses the error flag. """

    return is_used_in_code(code, 'sipError')


def needs_old_error_flag(code):
    """ Return True if handwritten code uses the deprecated error flag. """

    return is_used_in_code(code, 'sipIsErr')


def needs_heap_copy(arg, using_copy_ctor=True):
    """ Return True if an argument (or result) needs to be copied to the heap.
    """

    # The type is a class or mapped type and not a pointer.
    if not arg.no_copy and arg.type in (ArgumentType.CLASS, ArgumentType.MAPPED) and len(arg.derefs) == 0:
        # We need a copy unless it is a non-const reference.
        if not arg.is_reference or arg.is_const:
            # We assume we can copy a mapped type.
            if arg.type is ArgumentType.MAPPED:
                return True

            klass = arg.definition

            # We can't copy an abstract class.
            if klass.is_abstract:
                return False

            # We can copy if we have a public copy ctor.
            if not klass.cannot_copy:
                return True

            # We can't copy if we must use a copy ctor.
            if using_copy_ctor:
                return False

            # We can copy if we have a public assignment operator.
            return not klass.cannot_assign

    return False


def _needs_new_instance(arg):
    """ Return True if the argument type means an instance needs to be created
    on the heap to pass back to Python.
    """

    if not arg.is_in and arg.is_out:
        if arg.is_reference and len(arg.derefs) == 0:
            return True

        if not arg.is_reference and len(arg.derefs) == 1:
            return True

    return False


def _release_gil(gil_action, bindings):
    """ Return True if the GIL is to be released. """

    return bindings.release_gil if gil_action is GILAction.DEFAULT else gil_action is GILAction.RELEASE


def _sequence_support(sf, spec, klass, overload):
    """ Generate extra support for sequences because the class has an overload
    that has been annotated with __len__.
    """

    # We require a single int argument.
    if len(overload.py_signature.args) != 1:
        return

    arg0 = overload.py_signature.args[0]

    if not py_as_int(arg0):
        return

    # At the moment all we do is check that an index to __getitem__ is within
    # range so that the class supports Python iteration.  In the future we
    # should add support for negative indices, slices, __setitem__ and
    # __delitem__ (which will require enhancements to the sip module ABI).
    if overload.common.py_slot is PySlot.GETITEM:
        index_arg = fmt_argument_as_name(spec, arg0, 0)

        sf.write(
f'''            if ({index_arg} < 0 || {index_arg} >= sipCpp->{klass.len_cpp_name}())
            {{
                PyErr_SetNone(PyExc_IndexError);
                return SIP_NULLPTR;
            }}

''')


def _try(sf, bindings, throw_args):
    """ Generate the try block for a call. """

    # Generate the block if there was no throw specifier, or a non-empty throw
    # specifier.
    if _handling_exceptions(bindings, throw_args):
        sf.write(
'''            try
            {
''')


def _catch(sf, spec, bindings, py_signature, throw_args, release_gil):
    """ Generate the catch blocks for a call. """

    if _handling_exceptions(bindings, throw_args):
        use_handler = (spec.abi_version >= (13, 1) or (spec.abi_version >= (12, 9) and spec.abi_version < (13, 0)))

        sf.write('            }\n')

        if not use_handler:
            if throw_args is not None:
                for exception in throw_args.arguments:
                    catch_block(sf, spec, exception, py_signature=py_signature,
                            release_gil=release_gil)
            elif spec.module.default_exception is not None:
                catch_block(sf, spec, spec.module.default_exception,
                        py_signature=py_signature, release_gil=release_gil)

        sf.write(
'''            catch (...)
            {
''')

        if release_gil:
            sf.write(
'''                Py_BLOCK_THREADS

''')

        _delete_outs(sf, spec, py_signature)
        _delete_temporaries(sf, spec, py_signature)

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


def catch_block(sf, spec, exception, py_signature=None, release_gil=False):
    """ Generate a single catch block. """

    exception_fq_cpp_name = exception.iface_file.fq_cpp_name

    # The global scope is stripped from the exception name to be consistent
    # with older versions of SIP.
    exception_cpp_stripped = exception_fq_cpp_name.cpp_stripped(STRIP_GLOBAL)

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
        _delete_outs(sf, spec, py_signature)
        _delete_temporaries(sf, spec, py_signature)
        result = 'SIP_NULLPTR'
    else:
        result = 'true'

    # See if the exception is a wrapped class.
    if exception.class_exception is not None:
        exception_cpp = exception_fq_cpp_name.as_cpp

        sf.write(
f'''                /* Hope that there is a valid copy ctor. */
                {exception_cpp} *sipExceptionCopy = new {exception_cpp}(sipExceptionRef);

                sipRaiseTypeException({get_gto_name(exception)}, sipExceptionCopy);
''')
    else:
        sf.write_code(exception.raise_code)

    sf.write(
f'''
                return {result};
            }}
''')


def _handling_exceptions(bindings, throw_args):
    """ Return True if exceptions from a callable are being handled. """

    # Handle any exceptions if there was no throw specifier, or a non-empty
    # throw specifier.
    return bindings.exceptions and (throw_args is None or throw_args.arguments is not None)
