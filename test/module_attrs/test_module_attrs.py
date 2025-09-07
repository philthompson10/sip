# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from sys import getrefcount

from utils import SIPTestCase


class ModuleAttrsTestCase(SIPTestCase):
    """ Test the support for module attributes. """

    def test_attrs_bool(self):
        """ Test the support for bool and _Bool attributes. """

        import module_attrs_module as mod

        self.assertIs(mod.bool_attr, True)
        mod.bool_attr = False
        self.assertIs(mod.bool_attr, False)
        mod.bool_attr = 10
        self.assertIs(mod.bool_attr, True)

        self.assertIs(mod._Bool_attr, True)
        mod._Bool_attr = False
        self.assertIs(mod._Bool_attr, False)
        mod._Bool_attr = 10
        self.assertIs(mod._Bool_attr, True)

    def test_attrs_byte(self):
        """ Test the support for char as integer attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.byte_attr, 10)
        mod.byte_attr = 20
        self.assertEqual(mod.byte_attr, 20)

    def test_attrs_sbyte(self):
        """ Test the support for signed char as integer attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.sbyte_attr, -10)
        mod.sbyte_attr = 20
        self.assertEqual(mod.sbyte_attr, 20)

    def test_attrs_ubyte(self):
        """ Test the support for unsigned char as integer attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.ubyte_attr, 10)
        mod.ubyte_attr = 20
        self.assertEqual(mod.ubyte_attr, 20)

    def test_attrs_short(self):
        """ Test the support for short attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.short_attr, -10)
        mod.short_attr = 20
        self.assertEqual(mod.short_attr, 20)

    def test_attrs_ushort(self):
        """ Test the support for unsigned short attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.ushort_attr, 10)
        mod.ushort_attr = 20
        self.assertEqual(mod.ushort_attr, 20)

    def test_attrs_int(self):
        """ Test the support for int attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.int_attr, -10)
        mod.int_attr = 20
        self.assertEqual(mod.int_attr, 20)

        # Check the C++ value has changed and not the module dict.
        self.assertEqual(mod.get_int_attr(), 20)

    def test_attrs_uint(self):
        """ Test the support for unsigned int attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.uint_attr, 10)
        mod.uint_attr = 20
        self.assertEqual(mod.uint_attr, 20)

    def test_attrs_long(self):
        """ Test the support for long attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.long_attr, -10)
        mod.long_attr = 20
        self.assertEqual(mod.long_attr, 20)

    def test_attrs_ulong(self):
        """ Test the support for unsigned long attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.ulong_attr, 10)
        mod.ulong_attr = 20
        self.assertEqual(mod.ulong_attr, 20)

    def test_attrs_longlong(self):
        """ Test the support for long long attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.longlong_attr, -10)
        mod.longlong_attr = 20
        self.assertEqual(mod.longlong_attr, 20)

    def test_attrs_ulonglong(self):
        """ Test the support for unsigned long long attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.ulonglong_attr, 10)
        mod.ulonglong_attr = 20
        self.assertEqual(mod.ulonglong_attr, 20)

    def test_attrs_pyhasht(self):
        """ Test the support for Py_hash_t attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.pyhasht_attr, -10)
        mod.pyhasht_attr = 20
        self.assertEqual(mod.pyhasht_attr, 20)

    def test_attrs_pyssizet(self):
        """ Test the support for Py_ssize_t attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.pyssizet_attr, -10)
        mod.pyssizet_attr = 20
        self.assertEqual(mod.pyssizet_attr, 20)

    def test_attrs_sizet(self):
        """ Test the support for size_t attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.sizet_attr, 10)
        mod.sizet_attr = 20
        self.assertEqual(mod.sizet_attr, 20)

    def test_attrs_float(self):
        """ Test the support for float attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.float_attr, 10.)
        mod.float_attr = 20.
        self.assertEqual(mod.float_attr, 20.)

    def test_attrs_double(self):
        """ Test the support for double attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.double_attr, 10.)
        mod.double_attr = 20.
        self.assertEqual(mod.double_attr, 20.)

    def test_attrs_char(self):
        """ Test the support for char attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.char_attr, b'\x0a')
        mod.char_attr = b'\x14'
        self.assertEqual(mod.char_attr, b'\x14')

    def test_attrs_char_ascii(self):
        """ Test the support for ASCII char attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.char_ascii_attr, 'A')
        mod.char_ascii_attr = 'Z'
        self.assertEqual(mod.char_ascii_attr, 'Z')

    def test_attrs_char_latin1(self):
        """ Test the support for Latin-1 char attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.char_latin1_attr, '£')
        mod.char_latin1_attr = '§'
        self.assertEqual(mod.char_latin1_attr, '§')

    def test_attrs_char_utf8(self):
        """ Test the support for UTF-8 char attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.char_utf8_attr, 'A')
        mod.char_utf8_attr = 'Z'
        self.assertEqual(mod.char_utf8_attr, 'Z')

    def test_attrs_schar(self):
        """ Test the support for signed char attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.schar_attr, b'\x0a')
        mod.schar_attr = b'\x14'
        self.assertEqual(mod.schar_attr, b'\x14')

    def test_attrs_uchar(self):
        """ Test the support for unsigned char attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.uchar_attr, b'\x0a')
        mod.uchar_attr = b'\x14'
        self.assertEqual(mod.uchar_attr, b'\x14')

    def test_attrs_wchar(self):
        """ Test the support for wchar_t attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.wchar_attr, 'β')
        mod.wchar_attr = 'α'
        self.assertEqual(mod.wchar_attr, 'α')

    def test_attrs_string(self):
        """ Test the support for string attributes. """

        import module_attrs_module as mod

        self.assertIsNone(mod.string_attr)

        with self.assertRaises(ValueError):
            mod.string_attr = b'bad'

        self.assertEqual(mod.string_attr_const, b'str')

        mod.string_attr_const = b'new_str'
        self.assertEqual(mod.string_attr_const, b'new_str')

        mod.string_attr_const = None
        self.assertIsNone(mod.string_attr_const)

    def test_attrs_string_ascii(self):
        """ Test the support for ASCII string attributes. """

        import module_attrs_module as mod

        self.assertIsNone(mod.string_ascii_attr)

        with self.assertRaises(ValueError):
            mod.string_ascii_attr = 'bad'

        self.assertEqual(mod.string_ascii_attr_const, 'str')

        mod.string_ascii_attr_const = 'new_str'
        self.assertEqual(mod.string_ascii_attr_const, 'new_str')

        mod.string_ascii_attr_const = None
        self.assertIsNone(mod.string_ascii_attr_const)

        mod.string_ascii_attr_const = b'bytes'
        self.assertEqual(mod.string_ascii_attr_const, 'bytes')

    def test_attrs_string_latin1(self):
        """ Test the support for Latin-1 string attributes. """

        import module_attrs_module as mod

        self.assertIsNone(mod.string_latin1_attr)

        with self.assertRaises(ValueError):
            mod.string_latin1_attr = 'bad'

        self.assertEqual(mod.string_latin1_attr_const, '££')

        mod.string_latin1_attr_const = '§§'
        self.assertEqual(mod.string_latin1_attr_const, '§§')

        mod.string_latin1_attr_const = None
        self.assertIsNone(mod.string_latin1_attr_const)

        mod.string_latin1_attr_const = '££'.encode('latin-1')
        self.assertEqual(mod.string_latin1_attr_const, '££')

    def test_attrs_string_utf8(self):
        """ Test the support for UTF-8 string attributes. """

        import module_attrs_module as mod

        self.assertIsNone(mod.string_utf8_attr)

        with self.assertRaises(ValueError):
            mod.string_utf8_attr = 'bad'

        self.assertEqual(mod.string_utf8_attr_const, '2H₂ + O₂ ⇌ 2H₂O')

        mod.string_utf8_attr_const = 'ሲተረጉሙ ይደረግሙ።'
        self.assertEqual(mod.string_utf8_attr_const, 'ሲተረጉሙ ይደረግሙ።')

        mod.string_utf8_attr_const = None
        self.assertIsNone(mod.string_utf8_attr_const)

        mod.string_utf8_attr_const = 'Καλημέρα'.encode('utf-8')
        self.assertEqual(mod.string_utf8_attr_const, 'Καλημέρα')

    def test_attrs_sstring(self):
        """ Test the support for signed string attributes. """

        import module_attrs_module as mod

        self.assertIsNone(mod.sstring_attr)

        with self.assertRaises(ValueError):
            mod.sstring_attr = b'bad'

        self.assertEqual(mod.sstring_attr_const, b'str')

        mod.sstring_attr_const = b'new_str'
        self.assertEqual(mod.sstring_attr_const, b'new_str')

        mod.sstring_attr_const = None
        self.assertIsNone(mod.sstring_attr_const)

    def test_attrs_ustring(self):
        """ Test the support for unsigned string attributes. """

        import module_attrs_module as mod

        self.assertIsNone(mod.ustring_attr)

        with self.assertRaises(ValueError):
            mod.ustring_attr = b'bad'

        self.assertEqual(mod.ustring_attr_const, b'str')

        mod.ustring_attr_const = b'new_str'
        self.assertEqual(mod.ustring_attr_const, b'new_str')

        mod.ustring_attr_const = None
        self.assertIsNone(mod.ustring_attr_const)

    def test_attrs_voidptr(self):
        """ Test the support for void pointer attributes. """

        import module_attrs_module as mod

        vp = mod.voidptr_attr
        self.assertEqual(vp.asstring(size=5), b'bytes')
        self.assertTrue(vp.getwriteable())

        mod.voidptr_attr = None
        self.assertIsNone(mod.voidptr_attr)

        const_vp = mod.voidptr_const_attr
        self.assertEqual(const_vp.asstring(size=11), b'bytes const')
        self.assertFalse(const_vp.getwriteable())

        mod.voidptr_const_attr = None
        self.assertIsNone(mod.voidptr_const_attr)

    def test_attrs_pyobject(self):
        """ Test the support for Python object attributes. """

        import module_attrs_module as mod

        # Note that SIP does not check the Python types of these attributes
        # (which is really a bug) so we don't test the different types.
        self.assertIsNone(mod.pyobject_attr)

        obj = object()
        obj_refcount = getrefcount(obj)

        mod.pyobject_attr = obj
        self.assertIs(mod.pyobject_attr, obj)
        self.assertEqual(getrefcount(obj), obj_refcount + 1)

    def test_del_attrs(self):
        """ Test the support for deleting module attributes. """

        import module_attrs_module as mod

        with self.assertRaises(AttributeError):
            del mod.int_attr

    def test_nonwrapped_attrs(self):
        """ Test the support for non-wrapped module attributes. """

        import module_attrs_module as mod

        with self.assertRaises(AttributeError):
            mod.foo

        mod.foo = 'bar'
        self.assertEqual(mod.foo, 'bar')

        del mod.foo

        with self.assertRaises(AttributeError):
            mod.foo

    def test_const_types(self):
        """ Test the support for const module attributes. """

        import module_attrs_module as mod

        self.assertEqual(mod.int_attr_const, 10)

        with self.assertRaises(ValueError):
            mod.int_attr_const = 0

    def test_getters_and_setters(self):
        """ Test the support for %GetCode and %SetCode. """

        import module_attrs_module as mod

        self.assertEqual(mod.int_attr_getter, 20)

        mod.int_attr_setter = 20
        self.assertEqual(mod.int_attr_setter, 40)

        with self.assertRaises(NameError):
            mod.int_attr_bad_setter = 0

    def test_no_setter_types(self):
        """ Test the support for the /NoSetter/ annotation. """

        import module_attrs_module as mod

        self.assertEqual(mod.int_attr_no_setter, 10)

        with self.assertRaises(ValueError):
            mod.int_attr_no_setter = 0

    def test_nonwrapped_attrs(self):
        """ Test that non-wrapped attributes are handled properly. """

        import module_attrs_module as mod

        mod.nonwrapped_int = 10
        self.assertEqual(mod.nonwrapped_int, 10)

    def test_py_name_attrs(self):
        """ Test the support for the /PyName/ annotation. """

        import module_attrs_module as mod

        self.assertEqual(mod.py_int_attr, 10)
