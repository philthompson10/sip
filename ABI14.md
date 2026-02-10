# ABI v14 Development

This document covers various aspects of the development of ABI v14 (aka
issue/34).  This development targets Python v3.15 to implement support for
sub-interpreters and free-threading.


## Development Process

This development will significantly impact existing code (code that has limited
tests).  The normal development process would squash-merge the completed
development into the `develop` branch in one go.  This approach is risky with
regard to the stability of the existing code.  A different process will be
adopted in order to minimise this risk.

The development work will be done on a `working` branch.

A commit to the `working` branch must contain changes to new code **or** to
existing code **but not both**.  Commits that affect existing code are
cherry-picked into the `develop` branch.

When the development is complete (ie. ready for external testing) then this
file is removed and all outstanding changes in the `working` branch are
squash-merged into the `develop` branch.  The `working` branch can then be
deleted.


## API Changes

This section describes API changes when ABI v14 is used.  These will be
reflected in the documentation (and in any bindings-specific documentation).

The behaviour when setting the value of static attributes (of either modules or
classes) has changed.  With ABI v14 the value of the underlying C/C++ is
changed accordingly and may involve a type conversion of the new value.  With
older ABIs the Python attribute would be changed (possibly to a different type
entirely) but the underlying value would not change.


## ABI Changes

This is an ad-hoc list of changes when ABI v14 is used.  These will be
reflected in the documentation.

Python v3.15 or later will be required.

`sip-module` now takes any number of `--option` arguments to specify the
configuration of the module.

The `%AccessCode` directive has been removed.  `%GetCode` should be used
instead.

`sip.cast()` is no longer supported.

The `%SipModuleConfiguration` directive has been added.

All `__hash__` handwritten code must return `Py_hash_t`.

All `__len__` handwritten code must return `Py_ssize_t`.

Types in the `sip` module now have fully qualified names (eg.
`PyQt5.sip.wrapper`.

The `%GetCode` and `%SetCode` directives are now supported for module
attributes.

The deprecated `sipIsErr` error flag is no longer supported.

Module attributes can now be modified with the same restrictions as class
attributes.  Therefore module attributes that are constants can no longer be
modified.

`sipBuildResult()` `b` format character takes a `_Bool` argument.

`sipFindType()` has been replaced with `sipFindTypeID()`.

`sipTypeAsPyTypeObject()` has been replaced with `sipGetPyType()`.

`sipConvertFromEnum()` now takes the address of the enum value to convert
rather than its value.

`sipConvertToEnum()` now takes the address of the enum value to convert as an
argument rather than return its value.

The signatures of the following public API calls have changed:

    `sipBuildResult()`
    `sipConvertToBool()`
    `sipKeepReference()`

The user data that can be attached to a wrapped type is now a `PyObject`
instead of an arbitrary pointer.  `sipSetTypeUserData()` will take a strong
reference to the object and `sipGetTypeUserData()` will return a new reference
to the object.

The following public API calls have been removed:

    `sipRegisterAttributeGetter()`

The `sipWrapperType`, `sipWrapper` and `sipSimpleWrapper` structs are no longer
visible.

All API calls that used to take a `sipWrapperType *` now take a
`PyTypeObject *`.

All API calls that used to take a `sipWrapper *` or a `sipSimpleWrapper *` now
take a `PyObject *`.


## Rejected Ideas

### PEP 539

This PEP describes the C API for thread state storage.  Using it would allow
the handling of threads by the `sip` module to be (slightly) simplified and
(maybe) made safer.  However there doesn't seem to be a mechanism to allow the
data being stored to be freed (for all threads) when the `sip` module is
garbage collected.


### PEP 697

This PEP describes using a negative `basicsize` for an extension type and using
`PyObject_GetTypeData()` to access the type's data rather than casting
`PyObject` to the specific type.  The effect is to make `PyObject` (and
`PyTypeObject`) opaque and able to be used with the stable ABI.

This would be necessary in any attempt to implement the `sip` module using the
limited API.

Experimentation showed that:

- there was significant disruption to the code
- it wasn't clear what the error checking strategy should be (assume success,
  test failures with `assert()`, report failures as exceptions)
- other enhancements to the limited API would still be needed before it could
  be used to implement the `sip` module.


### Managed Dicts

`Py_TPFLAGS_MANAGED_DICT` was added to Python v3.12 but with little explanation
of its purpose.


## TODO

These are the remaining broad areas of work.

- Mapped type attributes.
- Port any remaining tests.
- Resolve issue #100 (`const` enforcement).
- Use `tp_token` instead of `wt_td`?
- Abstract classes.
- Imports.
- Events.
- Buffer protocol.
- Pickling.
- Implement and test the BrokenTypeNames sip module configuration option.
