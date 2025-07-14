# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from utils import SIPTestCase


class ModuleAttrsTestCase(SIPTestCase):
    """ Test the support for module attributes. """

    @classmethod
    def setUpClass(cls):
        """ Set up the test case. """

        super().setUpClass()

        import module_attrs_module as ma

        cls.ma = ma

    def test_const_types(self):
        """ Test the support for const module attributes. """

        self.assertEqual(self.ma.int_attr_const, 50)

        with self.assertRaises(ValueError):
            self.ma.int_attr_const = 0

    def test_nonwrapped_attrs(self):
        """ Test that non-wrapped attributes are handled properly. """

        self.ma.nonwrapped_int = 100
        self.assertEqual(self.ma.nonwrapped_int, 100)

    def test_pod_types(self):
        """ Test the support for POD module attributes. """

        self.assertEqual(self.ma.int_attr, 10)
        self.ma.int_attr = 20
        self.assertEqual(self.ma.int_attr, 20)
