# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from enum import IntFlag

from .exceptions import UserException


class SipModuleConfiguration(IntFlag):
    """ The different aspects of the sip module's configuration for ABI v14 and
    later.  The flag values must not be changed.  The name of a flag is visible
    to the user, hence the use of upper camel case.
    """

    # Use a custom Python object to wrap enums.
    CustomEnums = 0x0001

    # Use standard Python enum objects to wrap enums.
    PyEnums = 0x0002


def apply_module_defaults(module_configuration):
    """ Apply the default module configuration options for any that haven't
    been set explicitly.
    """

    # Make sure Python enums are used if custom enums is not specified.
    if SipModuleConfiguration.CustomEnums not in module_configuration:
        module_configuration |= SipModuleConfiguration.PyEnums

    return module_configuration


def apply_module_option(module_configuration, option_name):
    """ Apply a module configuration option while checking it is valid. """

    try:
        option = SipModuleConfiguration.__members__[option_name]
    except KeyError:
        raise UserException(
                f"'{option_name}' is not a supported module configuration option")

    if option is SipModuleConfiguration.CustomEnums and SipModuleConfiguration.PyEnums in module_configuration:
        raise UserException(
                "CustomEnums cannot be set because PyEnums has already been specified")

    if option is SipModuleConfiguration.PyEnums and SipModuleConfiguration.CustomEnums in module_configuration:
        raise UserException(
                "PyEnums cannot be set because CustomEnums has already been specified")

    return module_configuration | option
