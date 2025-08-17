# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from utils import SIPTestCase


class StandaloneSipModuleTestCase(SIPTestCase):
    """ Test the support for a standalone (ie. imported) sip module. """

    namespace = 'ns'
    use_sip_module = True

    @classmethod
    def setUpClass(cls):
        """ Set up the test case. """

        super().setUpClass()

        import ns.standalone_sip_module_module as w_mod
        import ns.sip as s_mod

        cls.w_mod = w_mod
        cls.s_mod = s_mod

    def test_wrapped_module_name(self):
        """ Test the support for a wrapped module's __name__. """

        self.assertEqual(self.w_mod.__name__,
                'ns.standalone_sip_module_module')

    def test_toplevel_wrapped_types(self):
        """ Test the support for toplevel wrapped types. """

        self.assertEqual(self.w_mod.Klass.__module__,
                'ns.standalone_sip_module_module')
        self.assertEqual(self.w_mod.Klass.__name__, 'Klass')
        self.assertEqual(self.w_mod.Klass.__qualname__, 'Klass')

    def test_nested_wrapped_types(self):
        """ Test the support for nested wrapped types. """

        self.assertEqual(self.w_mod.Klass.Nested.__module__,
                'ns.standalone_sip_module_module')
        self.assertEqual(self.w_mod.Klass.Nested.__name__, 'Nested')
        self.assertEqual(self.w_mod.Klass.Nested.__qualname__, 'Klass.Nested')
