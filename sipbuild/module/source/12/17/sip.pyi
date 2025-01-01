# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from typing import Any, Generic, Iterable, overload, Sequence, TypeVar, Union


# PEP 484 has no explicit support for the buffer protocol so we just name types
# we know that implement it.
Buffer = Union[bytes, bytearray, memoryview, 'array', 'voidptr']


# Constants.
SIP_VERSION = ...       # type: int
SIP_VERSION_STR = ...   # type: str


# The bases for SIP generated types.
class wrappertype:
    def __init__(self, *args, **kwargs) -> None: ...

class simplewrapper:
    def __init__(self, *args, **kwargs) -> None: ...

class wrapper(simplewrapper): ...


# The array type.
_T = TypeVar('_T')

class array(Sequence[_T], Generic[_T]):

    @overload
    def __getitem__(self, key: int) -> _T: ...
    @overload
    def __getitem__(self, key: slice) -> 'array[_T]': ...

    @overload
    def __setitem__(self, key: int, value: _T) -> None: ...
    @overload
    def __setitem__(self, key: slice, value: Iterable[_T]) -> None: ...

    @overload
    def __delitem__(self, key: int) -> None: ...
    @overload
    def __delitem__(self, key: slice) -> None: ...

    def __len__(self) -> int: ...


# The voidptr type.
class voidptr:

    def __init__(self, addr: Union[int, Buffer], size: int = -1, writeable: bool = True) -> None: ...

    def __int__(self) -> int: ...

    @overload
    def __getitem__(self, i: int) -> bytes: ...

    @overload
    def __getitem__(self, s: slice) -> 'voidptr': ...

    def __len__(self) -> int: ...

    def __setitem__(self, i: Union[int, slice], v: Buffer) -> None: ...

    def asarray(self, size: int = -1) -> array[int]: ...

    # Python doesn't expose the capsule type.
    def ascapsule(self) -> Any: ...

    def asstring(self, size: int = -1) -> bytes: ...

    def getsize(self) -> int: ...

    def getwriteable(self) -> bool: ...

    def setsize(self, size: int) -> None: ...

    def setwriteable(self, writeable: bool) -> None: ...


# Remaining functions.
def assign(obj: simplewrapper, other: simplewrapper) -> None: ...
def cast(obj: simplewrapper, type: wrappertype) -> simplewrapper: ...
def delete(obj: simplewrapper) -> None: ...
def dump(obj: simplewrapper) -> None: ...
def enableautoconversion(type: wrappertype, enable: bool) -> bool: ...
def enableoverflowchecking(enable: bool) -> bool: ...
def getapi(name: str) -> int: ...
def isdeleted(obj: simplewrapper) -> bool: ...
def ispycreated(obj: simplewrapper) -> bool: ...
def ispyowned(obj: simplewrapper) -> bool: ...
def setapi(name: str, version: int) -> None: ...
def setdeleted(obj: simplewrapper) -> None: ...
def setdestroyonexit(destroy: bool) -> None: ...
def settracemask(mask: int) -> None: ...
def transferback(obj: wrapper) -> None: ...
def transferto(obj: wrapper, owner: wrapper) -> None: ...
def unwrapinstance(obj: simplewrapper) -> None: ...
def wrapinstance(addr: int, type: wrappertype) -> simplewrapper: ...
