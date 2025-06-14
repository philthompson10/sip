# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from utils import SIPTestCase


class SeparateSipModuleTestCase(SIPTestCase):
    """ Test the support for a separate (ie. imported) sip module. """

    use_sip_module = True

    def test_separate_sip_module(self):
        """ Test the support importing a separate sip module. """

        import standalone_sip_module
