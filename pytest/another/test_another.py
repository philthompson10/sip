# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


def test_module___name__(module):
    assert module.__name__ == 'another_module'


def test_simplewrapper___module__(module):
    assert module.simplewrapper.__module__ == 'another_module'

def test_simplewrapper___name__(module):
    assert module.simplewrapper.__name__ == 'simplewrapper'

def test_simplewrapper___qualname__(module):
    assert module.simplewrapper.__qualname__ == 'simplewrapper'
