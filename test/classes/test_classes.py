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

    def ztest_simple_classes(self):
        """ Test the support for simple classes. """

        self.assertIsInstance(self.c_mod.Klass(), self.c_mod.Klass)

        self.assertEqual(self.c_mod.Klass.__module__, 'classes_module')
        self.assertEqual(self.c_mod.Klass.__name__, 'Klass')
        self.assertEqual(self.c_mod.Klass.__qualname__, 'Klass')

    def ztest_class_attributes(self):
        """ Test the support for class attributes. """

        with self.assertRaises(AttributeError):
            self.c_mod.Klass.foo

        self.c_mod.Klass.foo = 'bar'
        self.assertEqual(self.c_mod.Klass.foo, 'bar')

        del self.c_mod.Klass.foo

        with self.assertRaises(AttributeError):
            self.c_mod.Klass.foo

    def ztest_instance_attributes(self):
        """ Test the support for instance attributes. """

        klass = self.c_mod.Klass()

        with self.assertRaises(AttributeError):
            klass.foo

        klass.foo = 'bar'
        self.assertEqual(klass.foo, 'bar')

        del klass.foo

        with self.assertRaises(AttributeError):
            klass.foo

    def test_py_subclass(self):
        """ Test the support for Python sub-classes. """

        class SubK(self.c_mod.Klass):
            pass

        self.assertIsInstance(SubK(), self.c_mod.Klass)

        # TODO Test Klass instance and class attributes.
