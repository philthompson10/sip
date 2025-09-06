# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from utils import SIPTestCase


class ClassCallablesTestCase(SIPTestCase):
    """ Test the support for class callables. """

    @classmethod
    def setUpClass(cls):
        """ Set up the test case. """

        super().setUpClass()

        import class_callables_module as m

        cls.m = m

    def test_class_callables(self):
        """ Test the support for class callables. """

        self.assertEqual(self.m.Klass.get_s_attr_int(), 0)
        self.m.Klass.set_s_attr_int(10)
        self.assertEqual(self.m.Klass.get_s_attr_int(), 10)

    def test_instance_callables(self):
        """ Test the support for instance callables. """

        klass = self.m.Klass()

        self.assertEqual(klass.get_attr_int(), 0)
        klass.set_attr_int(10)
        self.assertEqual(klass.get_attr_int(), 10)

    def test_slot_call(self):
        """ Test the support for the __call__ slot. """

        klass = self.m.Klass()

        klass.set_attr_int(33)
        self.assertEqual(klass(2), 66)

    def test_slot_delitem(self):
        """ Test the support for the __delitem__ slot. """

        klass = self.m.Klass()

        original_count = klass.count()
        self.assertEqual(klass[2], 2)

        del klass[2]

        self.assertEqual(klass.count(), original_count - 1)
        self.assertEqual(klass[2], 3)

    def test_slot_eq(self):
        """ Test the support for the __eq__ slot. """

        klass = self.m.Klass()
        other = self.m.Klass()

        self.assertIs(klass == other, True)

        klass.set_attr_int(10)
        self.assertIs(klass == other, False)

        self.assertIs(klass == 100, False)

    def test_slot_getitem(self):
        """ Test the support for the __getitem__ slot. """

        klass = self.m.Klass()

        self.assertEqual(klass[2], 2)

        with self.assertRaises(IndexError):
            klass[-1]

        with self.assertRaises(IndexError):
            klass[klass.count()]

    def test_slot_len(self):
        """ Test the support for the __len__ slot. """

        klass = self.m.Klass()

        self.assertEqual(klass.count(), len(klass))

    def test_slot_neg(self):
        """ Test the support for the __neg__ slot. """

        klass = self.m.Klass()

        klass.set_attr_int(10)
        self.assertEqual(-klass, -10)

    def test_slot_setitem(self):
        """ Test the support for the __setitem__ slot. """

        klass = self.m.Klass()

        self.assertEqual(klass[2], 2)
        klass[2] = 20
        self.assertEqual(klass[2], 20)

        with self.assertRaises(IndexError):
            klass[-1] = 0

        with self.assertRaises(IndexError):
            klass[klass.count()] = 0
