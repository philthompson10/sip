# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2024 Phil Thompson <phil@riverbankcomputing.com>


from .parser_manager import ParserManager


def parse(spec, bindings, hex_version, encoding, include_dirs):
    """ Parse a .sip file and return a 2-tuple of a list of Module objects and
    a list of the .sip files that specify the module to be generated.  A
    UserException is raised if there was an error.
    """

    return ParserManager(spec, bindings, hex_version, encoding, include_dirs).parse(bindings.sip_file)
