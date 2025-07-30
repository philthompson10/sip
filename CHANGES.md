# ABI v14 Changes

This is an ad-hoc list of changes when ABI v14 is used.  Eventually these
should drive changes to the documentation.

The `%SipModuleConfiguration` directive has been added.

All `__hash__` handwritten code must return `Py_hash_t`.

All `__len__` handwritten code must return `Py_ssize_t`.

Types in the `sip` module now have fully qualified names (eg.
`PyQt5.sip.wrapper`.

The `%AccessCode` directive is no longer supported.

The `%GetCode` and `%SetCode` directives are now supported for module
attributes.

The deprecated `sipIsErr` error flag is no longer supported.

Module attributes can now be modified with the same restrictions as class
attributes.  Therefore module attributes that are constants can no longer be
modified.

`sipBuildResult()` `b` format character takes a `_Bool` argument.

`_Bool` is now a synonym of `bool` in `.sip` specification files.

`sipFindType()` has been replaced with `sipFindTypeID`.

The signatures of the following public API calls have changed:

    `sipBuildResult()`
    `sipConvertToBool()`
    `sipKeepReference()`
