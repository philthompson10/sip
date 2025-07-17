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

Module attributes are properly wrapped and can be modified (if the underlying
C/C++ type allows it).
