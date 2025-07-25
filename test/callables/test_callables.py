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

    def test_module_ret_void_no_args(self):
        """ Test the support a module void() function. """

        self.c_mod.module_procedure_called = False
        self.c_mod.module_void_ret_no_args()
        self.assertIs(self.c_mod.module_procedure_called, True)
