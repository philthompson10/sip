# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from utils import SIPTestCase


class EmbeddedSipModuleTestCase(SIPTestCase):
    """ Test the support for an embedded sip module. """

    @classmethod
    def setUpClass(cls):
        """ Set up the test case. """

        super().setUpClass()

        import embedded_sip_module_module as w_mod

        cls.w_mod = w_mod

    def test_wrapped_module_name(self):
        """ Test the support for a wrapped module's __name__. """

        self.assertEqual(self.w_mod.__name__, 'embedded_sip_module_module')

    def test_toplevel_wrapped_types(self):
        """ Test the support for toplevel wrapped types. """

        self.assertEqual(self.w_mod.Klass.__module__,
                'embedded_sip_module_module')
        self.assertEqual(self.w_mod.Klass.__name__, 'Klass')
        self.assertEqual(self.w_mod.Klass.__qualname__, 'Klass')

    def test_nested_wrapped_types(self):
        """ Test the support for nested wrapped types. """

        self.assertEqual(self.w_mod.Klass.Nested.__module__,
                'embedded_sip_module_module')
        self.assertEqual(self.w_mod.Klass.Nested.__name__, 'Nested')
        self.assertEqual(self.w_mod.Klass.Nested.__qualname__, 'Klass.Nested')

    def test_sip_types(self):
        """ Test the support for the sip types. """

        self.assertEqual(self.w_mod.wrappertype.__module__,
                'embedded_sip_module_module')
        self.assertEqual(self.w_mod.wrappertype.__name__, 'wrappertype')
        self.assertEqual(self.w_mod.wrappertype.__qualname__, 'wrappertype')

        self.assertEqual(self.w_mod.wrapper.__module__,
                'embedded_sip_module_module')
        self.assertEqual(self.w_mod.wrapper.__name__, 'wrapper')
        self.assertEqual(self.w_mod.wrapper.__qualname__, 'wrapper')

        self.assertEqual(self.w_mod.simplewrapper.__module__,
                'embedded_sip_module_module')
        self.assertEqual(self.w_mod.simplewrapper.__name__, 'simplewrapper')
        self.assertEqual(self.w_mod.simplewrapper.__qualname__,
                'simplewrapper')
