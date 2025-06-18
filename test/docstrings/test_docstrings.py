# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from utils import SIPTestCase


class DocstringsTestCase(SIPTestCase):
    """ Test the support for docstrings. """

    def test_ModuleDocstrings(self):
        """ Test the support for timelines. """

        import docstrings_module as dm

        self.assertEqual(dm.__doc__, 'Module')
