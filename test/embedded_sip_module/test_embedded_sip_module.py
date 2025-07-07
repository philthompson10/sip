# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from utils import SIPTestCase


class EmbeddedSipModuleTestCase(SIPTestCase):
    """ Test the support for an embedded sip module. """

    def test_embedded_sip_module(self):
        """ Test the support importing an embedded sip module. """

        import embedded_sip_module_module
