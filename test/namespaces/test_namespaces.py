# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from utils import SIPTestCase


class NamespacesTestCase(SIPTestCase):
    """ Test the support for namespaces. """

    def test_instantiation(self):
        """ Test if a namespace can be instantiated. """

        from namespaces_module import NS

        with self.assertRaises(TypeError):
            NS()

    def test_namespaces(self):
        """ Test the support for namespace attributes and callables. """

        from namespaces_module import NS

        with self.assertRaises(AttributeError):
            NS.foo

        NS.foo = 'bar'
        self.assertEqual(NS.foo, 'bar')

        del NS.foo

        with self.assertRaises(AttributeError):
            NS.foo

        self.assertEqual(NS.attr, 0)
        NS.attr = 10
        self.assertEqual(NS.attr, 10)

        # Check the C++ value has changed and not the type dict.
        self.assertEqual(NS.get_attr(), 10)

        with self.assertRaises(AttributeError):
            del NS.attr;
