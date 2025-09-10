# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from utils import SIPTestCase


class ClassMiscTestCase(SIPTestCase):
    """ Test the support for a variety of class features. """

    def test_base_types(self):
        """ Test if the base types can be instantiated. """

        from class_misc_module import simplewrapper, wrapper

        with self.assertRaises(TypeError):
            simplewrapper()

        with self.assertRaises(TypeError):
            wrapper()

    def test_plain_classes(self):
        """ Test the support for plain classes. """

        from class_misc_module import Klass

        self.assertEqual(len(Klass.__mro__), 4)
        self.assertIsInstance(Klass(), Klass)

    def test_nested_classes(self):
        """ Test the support for nested classes. """

        from class_misc_module import Klass

        self.assertIsInstance(Klass.Nested(), Klass.Nested)

    def test_py_subclass(self):
        """ Test the support for Python sub-classes. """

        from class_misc_module import Klass

        class SubK(Klass):
            pass

        self.assertIsInstance(SubK(), Klass)

    def test_simple_plain_class(self):
        """ Test the support for plain classes using simplewrapper. """

        from class_misc_module import SimpleKlass

        self.assertEqual(len(SimpleKlass.__mro__), 3)
        self.assertIsInstance(SimpleKlass(), SimpleKlass)

    def test_simple_py_subclass(self):
        """ Test the support for Python sub-classes using simplewrapper. """

        from class_misc_module import SimpleKlass

        class SimpleSubK(SimpleKlass):
            pass

        self.assertIsInstance(SimpleSubK(), SimpleKlass)
