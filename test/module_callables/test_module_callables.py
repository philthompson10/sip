# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from utils import SIPTestCase


class ModuleCallablesTestCase(SIPTestCase):
    """ Test the support for module callables. """

    def test_module_void_ret_no_args(self):
        """ Test the support a module void() function. """

        import module_callables_module as mod

        mod.module_procedure_called = False
        mod.module_void_ret_no_args()
        self.assertIs(mod.module_procedure_called, True)

    def test_module_int_ret_int_arg(self):
        """ Test the support a module int(int) function. """

        from module_callables_module import module_doubler

        self.assertEqual(module_doubler(3), 6)

    def test_module_int_int_ret_int_int_args(self):
        """ Test the support a module int(int, int, int *) function. """

        from module_callables_module import module_sum_diff

        res = module_sum_diff(3, 1)

        self.assertIsInstance(res, tuple)
        self.assertEqual(len(res), 2)
        self.assertEqual(res[0], 4)
        self.assertEqual(res[1], 2)

    def test_module_var_args(self):
        """ Test the support a module function with ellipsis. """

        from module_callables_module import module_var_args

        res = module_var_args(0, 1, 2)

        self.assertIsInstance(res, tuple)
        self.assertEqual(len(res), 2)
        self.assertEqual(res[0], 1)
        self.assertEqual(res[1], 2)

    def test_insufficient_args(self):
        """ Test insufficient arguments. """

        from module_callables_module import module_doubler

        self.assertRaises(TypeError, module_doubler, (), {})

    def test_excessive_args(self):
        """ Test excessive arguments. """

        from module_callables_module import module_doubler

        self.assertRaises(TypeError, module_doubler, (1, 1), {})

    def test_wrong_type_args(self):
        """ Test incorrect argument types. """

        from module_callables_module import module_doubler

        self.assertRaises(TypeError, module_doubler, ('1',), {})

    def test_no_kwd_args_support(self):
        """ Test the lack of support for keyword arguments. """

        from module_callables_module import module_doubler

        self.assertRaises(TypeError, module_doubler, (), dict(value=3))

    def test_bad_kwd_args(self):
        """ Test bad keyword arguments. """

        from module_callables_module import incr_optional_kwd_args

        self.assertRaises(TypeError, incr_optional_kwd_args, (1, ),
                dict(bad=3))
        self.assertRaises(TypeError, incr_optional_kwd_args, (1, 2),
                dict(incr=3))

    def test_optional_kwd_args_support(self):
        """ Test the support for optional keyword arguments. """

        from module_callables_module import incr_optional_kwd_args

        self.assertEqual(incr_optional_kwd_args(1), 2)
        self.assertEqual(incr_optional_kwd_args(1, 2), 3)
        self.assertEqual(incr_optional_kwd_args(1, incr=3), 4)

    def test_all_kwd_args_support(self):
        """ Test the support for all keyword arguments. """

        from module_callables_module import incr_all_kwd_args

        self.assertEqual(incr_all_kwd_args(1), 2)
        self.assertEqual(incr_all_kwd_args(1, 2), 3)
        self.assertEqual(incr_all_kwd_args(1, incr=3), 4)

        self.assertEqual(incr_all_kwd_args(value=1), 2)
        self.assertEqual(incr_all_kwd_args(value=1, incr=2), 3)
        self.assertEqual(incr_all_kwd_args(incr=3, value=1), 4)

        self.assertRaises(TypeError, incr_all_kwd_args, (), dict(incr=3))
