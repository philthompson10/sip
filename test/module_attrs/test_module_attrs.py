# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from utils import SIPTestCase


class ModuleAttrsTestCase(SIPTestCase):
    """ Test the support for module attributes. """

    @classmethod
    def setUpClass(cls):
        """ Set up the test case. """

        super().setUpClass()

        import module_attrs_module as ma

        cls.ma = ma

    def test_pod_types(self):
        """ Test the support POD module attributes. """

        self.assertEqual(self.ma.int_attr, 10)
