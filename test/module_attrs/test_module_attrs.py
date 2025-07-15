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

    def test_no_setter_types(self):
        """ Test the support for the /NoSetter/ annotation. """

        self.assertEqual(self.ma.int_attr_no_setter, 10)

        with self.assertRaises(ValueError):
            self.ma.int_attr_no_setter = 0

    def test_const_types(self):
        """ Test the support for const module attributes. """

        self.assertEqual(self.ma.int_attr_const, 10)

        with self.assertRaises(ValueError):
            self.ma.int_attr_const = 0

    def test_getters_and_setters(self):
        """ Test the support for %GetCode and %SetCode. """

        self.assertEqual(self.ma.int_attr_getter, 20)

        self.ma.int_attr_setter = 20
        self.assertEqual(self.ma.int_attr_setter, 40)

        with self.assertRaises(NameError):
            self.ma.int_attr_bad_setter = 0

    def test_nonwrapped_attrs(self):
        """ Test that non-wrapped attributes are handled properly. """

        self.ma.nonwrapped_int = 10
        self.assertEqual(self.ma.nonwrapped_int, 10)

    def test_pod_types(self):
        """ Test the support for POD module attributes. """

        self.assertEqual(self.ma.int_attr, 10)
        self.ma.int_attr = 20
        self.assertEqual(self.ma.int_attr, 20)

    def test_py_name_attrs(self):
        """ Test the support for the /PyName/ annotation. """

        self.assertEqual(self.ma.py_int_attr, 10)
