# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from abc import ABC, abstractmethod


class AbstractBackend(ABC):
    """ The abstract base class for backend code generators. """

    def __init__(self, spec):
        """ Initialise the backend. """

        self.spec = spec

    @staticmethod
    def factory(spec):
        """ Return an appropriate backend for the target ABI. """

        from .v12v13 import v12v13Backend as backend

        return backend(spec)

    @abstractmethod
    def g_composite_module_code(self, sf, py_debug):
        """ Generate the code for a composite module. """

        ...

    @abstractmethod
    def g_iface_file_code(self, sf, bindings, project, py_debug, buildable,
            iface_file, need_postinc):
        """ Generate the code specific to an interface file. """

        ...

    @abstractmethod
    def g_module_code(self, sf, bindings, project, py_debug, buildable):
        """ Generate the code for a module excluding the code specific to an
        interface file.  It returns a closure that will be passed to
        g_module_header_file().
        """

        ...

    @abstractmethod
    def g_module_header_file(self, sf, bindings, py_debug, closure):
        """ Generate the internal module API header file. """

        ...

    # The remaining public methods are snippet helpers.

    @abstractmethod
    def g_imported_module_api(self, sf, imported_module):
        """ Generate the representation of an imported module's API. """

        ...

    @abstractmethod
    def g_module_api(self, sf):
        """ Generate the representation of the module's API. """

        ...

    @abstractmethod
    def g_module_definition(self, sf):
        """ Generate the module definition structures. """

        ...

    @abstractmethod
    def g_sip_api(self, sf):
        """ Generate the macros for the SIP API. """

        ...

    def handle_module_closure(self, sf, closure):
        """ Handle the module closure (ie. that returned by g_module_code() and
        passed to g_module_header_file().
        """

        # This default implementation does nothing.
        pass
