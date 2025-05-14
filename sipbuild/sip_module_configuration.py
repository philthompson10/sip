# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from enum import IntFlag

from .exceptions import UserException


class SipModuleConfiguration(IntFlag):
    """ The different aspects of the sip module's configuration for ABI v14 and
    later.  The flag values must not be changed.  The name of a flag is visible
    to the user, hence the use of upper camel case.
    """

    # Use standard Python enum objects to wrap enums (default).
    PyEnums = 0x0001

    # Use a custom Python object to wrap enums.
    CustomEnums = 0x0002

    # Don't add a reference to the sip module as a root module (ie. in
    # site-packages (default).
    NoRootSipModule = 0x0004

    # Add a reference to the sip module as a root module (ie. in
    # site-packages).
    RootSipModule = 0x0008


def apply_module_defaults(module_configuration):
    """ Apply the default module configuration options for any that haven't
    been set explicitly.
    """

    if SipModuleConfiguration.CustomEnums not in module_configuration:
        module_configuration |= SipModuleConfiguration.PyEnums

    if SipModuleConfiguration.RootSipModule not in module_configuration:
        module_configuration |= SipModuleConfiguration.NoRootSipModule

    return module_configuration


def apply_module_option(module_configuration, option_name):
    """ Apply a module configuration option while checking it is valid. """

    try:
        option = SipModuleConfiguration.__members__[option_name]
    except KeyError:
        raise UserException(
                f"'{option_name}' is not a supported module configuration option")

    _verify_option(module_configuration, option,
            SipModuleConfiguration.PyEnums, SipModuleConfiguration.CustomEnums)
    _verify_option(module_configuration, option,
            SipModuleConfiguration.NoRootSipModule,
            SipModuleConfiguration.RootSipModule)

    return module_configuration | option


def _verify_option(module_configuration, new, option, converse):
    """ Verify that a new option is compatible with existing ones. """

    _check_option_conflicts(module_configuration, new, option, converse)
    _check_option_conflicts(module_configuration, new, converse, option)


def _check_option_conflicts(module_configuration, new, option, converse):
    """ Check to see if a new option conflicts with an existing one. """

    if new is option and converse in module_configuration:
        raise UserException(
                f"{new.name} cannot be set because {converse.name} has already been specified")
