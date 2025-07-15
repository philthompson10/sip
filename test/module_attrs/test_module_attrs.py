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

    def test_attrs_byte(self):
        """ Test the support for char as integer attributes. """

        self.assertEqual(self.ma.byte_attr, 10)
        self.ma.byte_attr = 20
        self.assertEqual(self.ma.byte_attr, 20)

    def test_attrs_sbyte(self):
        """ Test the support for signed char as integer attributes. """

        self.assertEqual(self.ma.sbyte_attr, -10)
        self.ma.sbyte_attr = 20
        self.assertEqual(self.ma.sbyte_attr, 20)

    def test_attrs_ubyte(self):
        """ Test the support for unsigned char as integer attributes. """

        self.assertEqual(self.ma.ubyte_attr, 10)
        self.ma.ubyte_attr = 20
        self.assertEqual(self.ma.ubyte_attr, 20)

    def test_attrs_short(self):
        """ Test the support for short attributes. """

        self.assertEqual(self.ma.short_attr, -10)
        self.ma.short_attr = 20
        self.assertEqual(self.ma.short_attr, 20)

    def test_attrs_ushort(self):
        """ Test the support for unsigned short attributes. """

        self.assertEqual(self.ma.ushort_attr, 10)
        self.ma.ushort_attr = 20
        self.assertEqual(self.ma.ushort_attr, 20)

    def test_attrs_int(self):
        """ Test the support for int attributes. """

        self.assertEqual(self.ma.int_attr, -10)
        self.ma.int_attr = 20
        self.assertEqual(self.ma.int_attr, 20)

    def test_attrs_uint(self):
        """ Test the support for unsigned int attributes. """

        self.assertEqual(self.ma.uint_attr, 10)
        self.ma.uint_attr = 20
        self.assertEqual(self.ma.uint_attr, 20)

    def test_attrs_long(self):
        """ Test the support for long attributes. """

        self.assertEqual(self.ma.long_attr, -10)
        self.ma.long_attr = 20
        self.assertEqual(self.ma.long_attr, 20)

    def test_attrs_ulong(self):
        """ Test the support for unsigned long attributes. """

        self.assertEqual(self.ma.ulong_attr, 10)
        self.ma.ulong_attr = 20
        self.assertEqual(self.ma.ulong_attr, 20)

    def test_attrs_longlong(self):
        """ Test the support for long long attributes. """

        self.assertEqual(self.ma.longlong_attr, -10)
        self.ma.longlong_attr = 20
        self.assertEqual(self.ma.longlong_attr, 20)

    def test_attrs_ulonglong(self):
        """ Test the support for unsigned long long attributes. """

        self.assertEqual(self.ma.ulonglong_attr, 10)
        self.ma.ulonglong_attr = 20
        self.assertEqual(self.ma.ulonglong_attr, 20)

    def test_attrs_pyhasht(self):
        """ Test the support for Py_hash_t attributes. """

        self.assertEqual(self.ma.pyhasht_attr, -10)
        self.ma.pyhasht_attr = 20
        self.assertEqual(self.ma.pyhasht_attr, 20)

    def test_attrs_pyssizet(self):
        """ Test the support for Py_ssize_t attributes. """

        self.assertEqual(self.ma.pyssizet_attr, -10)
        self.ma.pyssizet_attr = 20
        self.assertEqual(self.ma.pyssizet_attr, 20)

    def test_attrs_sizet(self):
        """ Test the support for size_t attributes. """

        self.assertEqual(self.ma.sizet_attr, 10)
        self.ma.sizet_attr = 20
        self.assertEqual(self.ma.sizet_attr, 20)

    def test_attrs_float(self):
        """ Test the support for float attributes. """

        self.assertEqual(self.ma.float_attr, 10.)
        self.ma.float_attr = 20.
        self.assertEqual(self.ma.float_attr, 20.)

    def test_attrs_double(self):
        """ Test the support for double attributes. """

        self.assertEqual(self.ma.double_attr, 10.)
        self.ma.double_attr = 20.
        self.assertEqual(self.ma.double_attr, 20.)

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

    def test_no_setter_types(self):
        """ Test the support for the /NoSetter/ annotation. """

        self.assertEqual(self.ma.int_attr_no_setter, 10)

        with self.assertRaises(ValueError):
            self.ma.int_attr_no_setter = 0

    def test_nonwrapped_attrs(self):
        """ Test that non-wrapped attributes are handled properly. """

        self.ma.nonwrapped_int = 10
        self.assertEqual(self.ma.nonwrapped_int, 10)

    def test_py_name_attrs(self):
        """ Test the support for the /PyName/ annotation. """

        self.assertEqual(self.ma.py_int_attr, 10)
