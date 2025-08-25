# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from utils import SIPTestCase


class ClassesTestCase(SIPTestCase):
    """ Test the support for classes. """

    @classmethod
    def setUpClass(cls):
        """ Set up the test case. """

        super().setUpClass()

        import classes_module as c_mod

        cls.c_mod = c_mod

    def test_plain_classes(self):
        """ Test the support for plain classes. """

        self.assertEqual(len(self.c_mod.Klass.__mro__), 4)
        self.assertIsInstance(self.c_mod.Klass(), self.c_mod.Klass)

    def test_nested_classes(self):
        """ Test the support for nested classes. """

        self.assertIsInstance(self.c_mod.Klass.Nested(),
                self.c_mod.Klass.Nested)

    def test_class_attributes(self):
        """ Test the support for class attributes. """

        with self.assertRaises(AttributeError):
            self.c_mod.Klass.foo

        self.c_mod.Klass.foo = 'bar'
        self.assertEqual(self.c_mod.Klass.foo, 'bar')

        del self.c_mod.Klass.foo

        with self.assertRaises(AttributeError):
            self.c_mod.Klass.foo

        self.assertEqual(self.c_mod.Klass.s_attr, 0)
        self.c_mod.Klass.s_attr = 10
        self.assertEqual(self.c_mod.Klass.s_attr, 10)

        with self.assertRaises(AttributeError):
            del self.c_mod.Klass.s_attr;

    def test_instance_attributes(self):
        """ Test the support for instance attributes. """

        klass = self.c_mod.Klass()

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

        with self.assertRaises(AttributeError):
            del klass.attr;

        with self.assertRaises(AttributeError):
            self.c_mod.Klass.attr

    def test_py_subclass(self):
        """ Test the support for Python sub-classes. """

        class SubK(self.c_mod.Klass):
            pass

        self.assertIsInstance(SubK(), self.c_mod.Klass)

    def test_simple_plain_class(self):
        """ Test the support for plain classes using simplewrapper. """

        self.assertEqual(len(self.c_mod.SimpleKlass.__mro__), 3)
        self.assertIsInstance(self.c_mod.SimpleKlass(), self.c_mod.SimpleKlass)

    def test_simple_py_subclass(self):
        """ Test the support for Python sub-classes using simplewrapper. """

        class SimpleSubK(self.c_mod.SimpleKlass):
            pass

        self.assertIsInstance(SimpleSubK(), self.c_mod.SimpleKlass)

    # TODO Test Klass instance and class wrapped attributes.
