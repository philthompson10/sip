# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from utils import SIPTestCase


class StandaloneSipModuleTestCase(SIPTestCase):
    """ Test the support for a standalone (ie. imported) sip module. """

    use_sip_module = True

    def test_standalone_sip_module(self):
        """ Test the support importing a standalone sip module. """

        import standalone_sip_module_module
