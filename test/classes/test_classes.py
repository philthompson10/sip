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

    def test_simple_classes(self):
        """ Test the support simple classes. """

        self.assertIsInstance(self.c_mod.Klass(), self.c_mod.Klass)

        self.assertEqual(self.c_mod.Klass.__module__, 'classes_module')
        self.assertEqual(self.c_mod.Klass.__name__, 'Klass')
        self.assertEqual(self.c_mod.Klass.__qualname__, 'Klass')
