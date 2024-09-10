# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2024 Phil Thompson <phil@riverbankcomputing.com>


from .misc import fmt_scoped_py_name
from .signature import fmt_signature_as_type_hint


def fmt_docstring(docstring):
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


def fmt_docstring_of_ctor(spec, ctor, klass):
    """ Return the automatically generated docstring for an ctor. """

    py_name = fmt_scoped_py_name(klass.scope, klass.py_name.name)
    signature = fmt_signature_as_type_hint(spec, ctor.py_signature,
            need_self=False, exclude_result=True)

    return py_name + signature



def fmt_docstring_of_overload(spec, overload, is_method=True):
    """ Return the automatically generated docstring for an overload. """

    need_self = is_method and not overload.is_static
    signature = fmt_signature_as_type_hint(spec, overload.py_signature,
            need_self=need_self)

    return overload.common.py_name.name + signature
