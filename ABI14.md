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

There will be a `staging` branch that only contains changes that affect
existing code.

A commit to the `working` branch must contain changes to new code **or** to
existing code **but not both**.  Commits that affect existing code are
cherry-picked into the `staging` branch.

Periodically the `staging` branch is squash-merged into the `develop` branch.
Exactly when this is done is a judgment call.  Doing it earlier and more often
increases the chances of problems coming to light sooner rather than later.

When the development is complete (ie. ready for external testing) then this
file is removed and all outstanding changes in the `working` branch are merged
to the `staging` branch.  These are then squash-merged into the `develop`
branch.  The `working` and `staging` branches can then be deleted.


## ABI Changes

This is an ad-hoc list of changes when ABI v14 is used.  Eventually these
should drive changes to the documentation.

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

`_Bool` is now a synonym of `bool` in `.sip` specification files.

`sipFindType()` has been replaced with `sipFindTypeID()`.

The signatures of the following public API calls have changed:

    `sipBuildResult()`
    `sipConvertToBool()`
    `sipKeepReference()`

The following public API calls have been removed:

    `sipRegisterAttributeGetter()`

The `sipWrapperType`, `sipWrapper` and `sipSimpleWrapper` structs are no longer
visible.

All API calls that used to take a `sipWrapperType *` now take a
`PyTypeObject *`.

All API calls that used to take a `sipWrapper *` or a `sipSimpleWrapper *` now
take a `PyObject *`.


## TODO

These are the remaining broad areas of work.

- Use `tp_token` instead of `wt_td`.
- Use a managed dict.
- Consider using `PyObject_GetTypeData()` and a negative `basicsize` within the
  `sip` module to faciliate using the limited API for the `sip` module.
- Mapped types.
- Python enums.
- Custom enums.
- Virtuals.
- Abstract classes.
- Imports.
- Events.
- Buffer protocol.
- Pickling.
- Port any legacy tests.
