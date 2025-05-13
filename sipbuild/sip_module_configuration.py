# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from enum import IntFlag


class SipModuleConfiguration(IntFlag):
    """ The different aspects of the sip module's configuration for ABI v14 and
    later.  The flag values must not be changed.  The name of a flag is visible
    to the user, hence the use of upper camel case.
    """

    # Use a custom Python object to wrap enums rather than standard Python enum
    # objects.
    LegacyEnums = 0x0001
