# ABI v14 Changes

This is an ad-hoc list of changes when ABI v14 is used.  Eventually these
should drive changes to the documentation.

Python v3.12 or later is required.

The `%AccessCode` directive has been removed.  `%GetCode` should be used
instead.

`sip.cast()` is no longer supported.

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

`sipFindType()` has been replaced with `sipFindTypeID()`.

The signatures of the following public API calls have changed:

    `sipBuildResult()`
    `sipConvertToBool()`
    `sipKeepReference()`

The following public API calls have been removed:

    `sipRegisterAttributeGetter()`


# TODO

These are the remaining broad areas of work.

- Use `PyObject_GetTypeData()` and a negative `basicsize`.
- Refactor the code generation.  There will be a outer framework managing the
  creation of generated files which calls a backend.  A backend will present a
  much more abstract API than they do now.  Code common to multiple backends
  (possibly configured by passing the target ABI version, or specific flags)
  will be contained in a package of snippets.
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
