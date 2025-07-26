# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from utils import SIPTestCase


class CallablesTestCase(SIPTestCase):
    """ Test the support for callables. """

    @classmethod
    def setUpClass(cls):
        """ Set up the test case. """

        super().setUpClass()

        import callables_module as c_mod

        cls.c_mod = c_mod

    def test_module_void_ret_no_args(self):
        """ Test the support a module void() function. """

        self.c_mod.module_procedure_called = False
        self.c_mod.module_void_ret_no_args()
        self.assertIs(self.c_mod.module_procedure_called, True)

    def test_module_int_ret_int_arg(self):
        """ Test the support a module int(int) function. """

        self.assertEqual(self.c_mod.module_doubler(3), 6)

    def test_module_int_int_ret_int_int_args(self):
        """ Test the support a module int(int, int, int *) function. """

        res = self.c_mod.module_sum_diff(3, 1)

        self.assertIsInstance(res, tuple)
        self.assertEqual(len(res), 2)
        self.assertEqual(res[0], 4)
        self.assertEqual(res[1], 2)

    def test_module_var_args(self):
        """ Test the support a module function with ellipsis. """

        res = self.c_mod.module_var_args(0, 1, 2)

        self.assertIsInstance(res, tuple)
        self.assertEqual(len(res), 2)
        self.assertEqual(res[0], 1)
        self.assertEqual(res[1], 2)

    def test_insufficient_args(self):
        """ Test insufficient arguments. """

        self.assertRaises(TypeError, self.c_mod.module_doubler, (), {})

    def test_excessive_args(self):
        """ Test excessive arguments. """

        self.assertRaises(TypeError, self.c_mod.module_doubler, (1, 1), {})

    def test_wrong_type_args(self):
        """ Test incorrect argument types. """

        self.assertRaises(TypeError, self.c_mod.module_doubler, ('1',), {})

    def test_no_kwd_args_support(self):
        """ Test the lack of support for keyword arguments. """

        self.assertRaises(TypeError, self.c_mod.module_doubler, (),
                dict(value=3))

    def test_bad_kwd_args(self):
        """ Test bad keyword arguments. """

        self.assertRaises(TypeError, self.c_mod.incr_optional_kwd_args, (1, ),
                dict(bad=3))
        self.assertRaises(TypeError, self.c_mod.incr_optional_kwd_args, (1, 2),
                dict(incr=3))

    def test_optional_kwd_args_support(self):
        """ Test the support for optional keyword arguments. """

        self.assertEqual(self.c_mod.incr_optional_kwd_args(1), 2)
        self.assertEqual(self.c_mod.incr_optional_kwd_args(1, 2), 3)
        self.assertEqual(self.c_mod.incr_optional_kwd_args(1, incr=3), 4)

    def test_all_kwd_args_support(self):
        """ Test the support for all keyword arguments. """

        self.assertEqual(self.c_mod.incr_all_kwd_args(1), 2)
        self.assertEqual(self.c_mod.incr_all_kwd_args(1, 2), 3)
        self.assertEqual(self.c_mod.incr_all_kwd_args(1, incr=3), 4)

        self.assertEqual(self.c_mod.incr_all_kwd_args(value=1), 2)
        self.assertEqual(self.c_mod.incr_all_kwd_args(value=1, incr=2), 3)
        self.assertEqual(self.c_mod.incr_all_kwd_args(incr=3, value=1), 4)

        self.assertRaises(TypeError, self.c_mod.incr_all_kwd_args, (),
                dict(incr=3))
