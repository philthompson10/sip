# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from utils import SIPTestCase


class MixinsTestCase(SIPTestCase):
    """ Test the support for mixins and cooperative multi-inheritance. """

    def test_mixins(self):
        """ Test the support for mixins and cooperative multi-inheritance. """

        from mixins_module import Klass, Mixin

        class TestClass(Klass, Mixin):
            pass

        test = TestClass(10, mixin_value=20, extra_kwarg=30)

        self.assertEqual(test.extra_kwarg, 30)

        with self.assertRaises(TypeError):
            TestClass(10, unknown_kwarg_1=30, mixin_value=20,
                    unknown_kwarg_2=40)

        self.assertEqual(test.get_attr(), 10)
        self.assertEqual(test.attr, 10)

        test.set_attr(11)
        self.assertEqual(test.get_attr(), 11)
        self.assertEqual(test.attr, 11)

        test.attr = (12)
        self.assertEqual(test.get_attr(), 12)
        self.assertEqual(test.attr, 12)

        self.assertEqual(test.get_mixin_attr(), 20)
        self.assertEqual(test.mixin_attr, 20)

        test.set_mixin_attr(21)
        self.assertEqual(test.get_mixin_attr(), 21)
        self.assertEqual(test.mixin_attr, 21)

        test.mixin_attr = (22)
        self.assertEqual(test.get_mixin_attr(), 22)
        self.assertEqual(test.mixin_attr, 22)
