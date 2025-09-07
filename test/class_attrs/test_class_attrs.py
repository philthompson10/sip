# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from utils import SIPTestCase


class ClassAttrsTestCase(SIPTestCase):
    """ Test the support for class attributes. """

    def test_plain_classes(self):
        """ Test the support for plain classes. """

        from class_attrs_module import Klass

        self.assertEqual(len(Klass.__mro__), 4)
        self.assertIsInstance(Klass(), Klass)

    def test_nested_classes(self):
        """ Test the support for nested classes. """

        from class_attrs_module import Klass

        self.assertIsInstance(Klass.Nested(), Klass.Nested)

    def test_class_attributes(self):
        """ Test the support for class attributes. """

        from class_attrs_module import Klass

        with self.assertRaises(AttributeError):
            Klass.foo

        Klass.foo = 'bar'
        self.assertEqual(Klass.foo, 'bar')

        del Klass.foo

        with self.assertRaises(AttributeError):
            Klass.foo

        self.assertEqual(Klass.s_attr, 0)
        Klass.s_attr = 10
        self.assertEqual(Klass.s_attr, 10)

        # Check the C++ value has changed and not the type dict.
        self.assertEqual(Klass.get_s_attr(), 10)

        with self.assertRaises(AttributeError):
            del Klass.s_attr;

    def test_instance_attributes(self):
        """ Test the support for instance attributes. """

        from class_attrs_module import Klass

        klass = Klass()

        with self.assertRaises(AttributeError):
            klass.foo

        klass.foo = 'bar'
        self.assertEqual(klass.foo, 'bar')

        del klass.foo

        with self.assertRaises(AttributeError):
            klass.foo

        self.assertEqual(klass.attr, 0)
        klass.attr = 10
        self.assertEqual(klass.attr, 10)

        # Check the C++ value has changed and not the instance dict.
        self.assertEqual(klass.get_attr(), 10)

        with self.assertRaises(AttributeError):
            del klass.attr;

        with self.assertRaises(AttributeError):
            Klass.attr

    def test_py_subclass(self):
        """ Test the support for Python sub-classes. """

        from class_attrs_module import Klass

        class SubK(Klass):
            pass

        self.assertIsInstance(SubK(), Klass)

    def test_simple_plain_class(self):
        """ Test the support for plain classes using simplewrapper. """

        from class_attrs_module import SimpleKlass

        self.assertEqual(len(SimpleKlass.__mro__), 3)
        self.assertIsInstance(SimpleKlass(), SimpleKlass)

    def test_simple_py_subclass(self):
        """ Test the support for Python sub-classes using simplewrapper. """

        from class_attrs_module import SimpleKlass

        class SimpleSubK(SimpleKlass):
            pass

        self.assertIsInstance(SimpleSubK(), SimpleKlass)
