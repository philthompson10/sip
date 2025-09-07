# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from utils import SIPTestCase


class StandaloneSipModuleTestCase(SIPTestCase):
    """ Test the support for a standalone (ie. imported) sip module. """

    namespace = 'ns'
    use_sip_module = True

    def test_wrapped_module_name(self):
        """ Test the support for a wrapped module's __name__. """

        import ns.standalone_sip_module_module as mod

        self.assertEqual(mod.__name__, 'ns.standalone_sip_module_module')

    def test_toplevel_wrapped_types(self):
        """ Test the support for toplevel wrapped types. """

        from ns.standalone_sip_module_module import Klass

        self.assertEqual(Klass.__module__, 'ns.standalone_sip_module_module')
        self.assertEqual(Klass.__name__, 'Klass')
        self.assertEqual(Klass.__qualname__, 'Klass')

    def test_nested_wrapped_types(self):
        """ Test the support for nested wrapped types. """

        from ns.standalone_sip_module_module import Klass

        self.assertEqual(Klass.Nested.__module__,
                'ns.standalone_sip_module_module')
        self.assertEqual(Klass.Nested.__name__, 'Nested')
        self.assertEqual(Klass.Nested.__qualname__, 'Klass.Nested')

    def test_sip_simplewrapper(self):
        """ Test the support for the simplewrapper type. """

        from ns.sip import simplewrapper

        self.assertEqual(simplewrapper.__module__, 'ns.sip')
        self.assertEqual(simplewrapper.__name__, 'simplewrapper')
        self.assertEqual(simplewrapper.__qualname__, 'simplewrapper')

    def test_sip_wrapper(self):
        """ Test the support for the simplewrapper type. """

        from ns.sip import wrapper

        self.assertEqual(wrapper.__module__, 'ns.sip')
        self.assertEqual(wrapper.__name__, 'wrapper')
        self.assertEqual(wrapper.__qualname__, 'wrapper')

    def test_sip_wrappertype(self):
        """ Test the support for the wrappertype type. """

        from ns.sip import wrappertype

        self.assertEqual(wrappertype.__module__, 'ns.sip')
        self.assertEqual(wrappertype.__name__, 'wrappertype')
        self.assertEqual(wrappertype.__qualname__, 'wrappertype')
