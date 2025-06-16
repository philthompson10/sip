# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


class CodeBackend:

    def __init__(self, spec):

        self.spec = spec

    @classmethod
    def factory(cls, spec):

        if spec.abi_version >= (14, 0):
            return cls(spec)

        from .legacy_code_backend import LegacyCodeBackend

        return LegacyCodeBackend(spec)
