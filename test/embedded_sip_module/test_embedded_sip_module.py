# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from utils import SIPTestCase


class EmbeddedSipModuleTestCase(SIPTestCase):
    """ Test the support for an embedded sip module. """

    def test_wrapped_module_name(self):
        """ Test the support for a wrapped module's __name__. """

        import embedded_sip_module_module as mod

        self.assertEqual(mod.__name__, 'embedded_sip_module_module')

    def test_toplevel_wrapped_types(self):
        """ Test the support for toplevel wrapped types. """

        from embedded_sip_module_module import Klass

        self.assertEqual(Klass.__module__, 'embedded_sip_module_module')
        self.assertEqual(Klass.__name__, 'Klass')
        self.assertEqual(Klass.__qualname__, 'Klass')

    def test_nested_wrapped_types(self):
        """ Test the support for nested wrapped types. """

        from embedded_sip_module_module import Klass

        self.assertEqual(Klass.Nested.__module__, 'embedded_sip_module_module')
        self.assertEqual(Klass.Nested.__name__, 'Nested')
        self.assertEqual(Klass.Nested.__qualname__, 'Klass.Nested')

    def test_simplewrapper(self):
        """ Test the support for the simplewrapper type. """

        from embedded_sip_module_module import simplewrapper

        self.assertEqual(simplewrapper.__module__,
                'embedded_sip_module_module')
        self.assertEqual(simplewrapper.__name__, 'simplewrapper')
        self.assertEqual(simplewrapper.__qualname__, 'simplewrapper')

    def test_wrapper(self):
        """ Test the support for the wrapper type. """

        from embedded_sip_module_module import wrapper

        self.assertEqual(wrapper.__module__, 'embedded_sip_module_module')
        self.assertEqual(wrapper.__name__, 'wrapper')
        self.assertEqual(wrapper.__qualname__, 'wrapper')

    def test_wrappertype(self):
        """ Test the support for the wrappertype type. """

        from embedded_sip_module_module import wrappertype

        self.assertEqual(wrappertype.__module__, 'embedded_sip_module_module')
        self.assertEqual(wrappertype.__name__, 'wrappertype')
        self.assertEqual(wrappertype.__qualname__, 'wrappertype')
