# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


import os

from ....exceptions import UserException
from ....sip_module_configuration import SipModuleConfiguration
from ....version import SIP_VERSION_STR

from ...python_slots import (is_hash_return_slot, is_inplace_number_slot,
        is_inplace_sequence_slot, is_int_arg_slot, is_int_return_slot,
        is_multi_arg_slot, is_number_slot, is_rich_compare_slot,
        is_ssize_return_slot, is_void_return_slot, is_zero_arg_slot)
from ...scoped_name import STRIP_GLOBAL, STRIP_NONE
from ...specification import (AccessSpecifier, Argument, ArgumentType,
        ArrayArgument, DocstringSignature, IfaceFileType, KwArgs, MappedType,
        PyQtMethodSpecifier, PySlot, QualifierType, Transfer, ValueType,
        WrappedClass, WrappedEnum)
from ...utils import find_method, py_as_int, same_signature

from ..formatters import (fmt_argument_as_cpp_type, fmt_argument_as_name,
        fmt_class_as_scoped_name, fmt_copying, fmt_enum_as_cpp_type,
        fmt_scoped_py_name, fmt_signature_as_cpp_declaration,
        fmt_signature_as_cpp_definition, fmt_signature_as_type_hint,
        fmt_value_list_as_cpp_expression)

from .backends import Backend
from .utils import (arg_is_small_enum, callable_overloads,
        get_convert_to_type_code, get_encoded_type, get_normalised_cached_name,
        get_slot_name, get_user_state_suffix, has_method_docstring,
        is_used_in_code, need_error_flag, py_scope, release_gil, skip_overload,
        type_needs_user_state)


def output_code(spec, bindings, project, buildable):
    """ Output the C/C++ code and add it to the given buildable. """

    module = spec.module
    py_debug = project.py_debug
    backend = Backend.factory(spec)

    if spec.is_composite:
        source_name = os.path.join(buildable.build_dir,
                'sip' + module.py_name + 'cmodule.c')

        with CompilationUnit(source_name, "Composite module code.", module, project, buildable, sip_api_file=False) as sf:
            _composite_module_code(backend, sf, py_debug)
    else:
        _module_code(backend, bindings, project, py_debug, buildable)


def _internal_api_header(backend, sf, bindings, py_debug, name_cache_list):
    """ Generate the C++ internal module API header file and return its path
    name.
    """

    spec = backend.spec
    module = spec.module
    module_name = spec.module.py_name

    # The include files.
    sf.write(
f'''#ifndef _{module_name}API_H
#define _{module_name}API_H
''')

    _declare_limited_api(sf, py_debug, module=module)
    _include_sip_h(sf, module)

    if backend.pyqt5_supported() or backend.pyqt6_supported():
        sf.write(
'''
#include <QMetaType>
#include <QThread>
''')

    # Define the qualifiers.
    qualifier_defines = []

    _append_qualifier_defines(module, bindings, qualifier_defines)

    for imported_module in module.all_imports:
        _append_qualifier_defines(imported_module, bindings, qualifier_defines)

    if len(qualifier_defines) != 0:
        sf.write('\n/* These are the qualifiers that are enabled. */\n')

        for qualifier_define in qualifier_defines:
            sf.write(qualifier_define + '\n')

        sf.write('\n')

    # Generate references to (potentially) shared strings.
    if name_cache_list is not None:
        sf.write(
'''
/*
 * Convenient names to refer to various strings defined in this module.
 * Only the class names are part of the public API.
 */
''')

        for cached_name in name_cache_list:
            sf.write(
f'''#define {backend.cached_name_ref(cached_name, as_nr=True)} {cached_name.offset}
#define {backend.cached_name_ref(cached_name)} &sipStrings_{module_name}[{cached_name.offset}]
''')

    # Generate the SIP API.
    backend.g_sip_api(sf, module_name)

    # The name strings.
    if name_cache_list is not None:
        sf.write(
f'''
/* The strings used by this module. */
extern const char sipStrings_{module_name}[];
''')

    _module_api(backend, sf, bindings)

    # TODO Move to the backend when everything else gets moved.
    if spec.target_abi < (14, 0):
        sf.write(
f'''
/* The SIP API, this module's API and the APIs of any imported modules. */
extern const sipAPIDef *sipAPI_{module_name};
extern sipExportedModuleDef sipModuleAPI_{module_name};
''')

        if len(module.needed_types) != 0:
            sf.write(f'extern sipTypeDef *sipExportedTypes_{module_name}[];\n')

    for imported_module in module.all_imports:
        imported_module_name = imported_module.py_name

        _imported_module_api(backend, sf, imported_module)

        if len(imported_module.needed_types) != 0:
            sf.write(f'extern sipImportedTypeDef sipImportedTypes_{module_name}_{imported_module_name}[];\n')

        if imported_module.nr_virtual_error_handlers != 0:
            sf.write(f'extern sipImportedVirtErrorHandlerDef sipImportedVirtErrorHandlers_{module_name}_{imported_module_name}[];\n')

        if imported_module.nr_exceptions != 0:
            sf.write(f'extern sipImportedExceptionDef sipImportedExceptions_{module_name}_{imported_module_name}[];\n')

    if backend.pyqt5_supported() or backend.pyqt6_supported():
        sf.write(
f'''
typedef const QMetaObject *(*sip_qt_metaobject_func)(sipSimpleWrapper *, sipTypeDef *);
extern sip_qt_metaobject_func sip_{module_name}_qt_metaobject;

typedef int (*sip_qt_metacall_func)(sipSimpleWrapper *, sipTypeDef *, QMetaObject::Call, int, void **);
extern sip_qt_metacall_func sip_{module_name}_qt_metacall;

typedef bool (*sip_qt_metacast_func)(sipSimpleWrapper *, const sipTypeDef *, const char *, void **);
extern sip_qt_metacast_func sip_{module_name}_qt_metacast;
''')

    # Handwritten code.
    sf.write_code(spec.exported_header_code)
    sf.write_code(module.module_header_code)

    # Make sure any header code needed by the default exception is included.
    if module.default_exception is not None:
        sf.write_code(module.default_exception.iface_file.type_header_code)

    # Note that we don't forward declare the virtual handlers.  This is because
    # we would need to #include everything needed for their argument types.
    sf.write('\n#endif\n')


def _make_part_name(buildable, module_name, part_nr, source_suffix):
    """ Return the filename of a source code part on the heap. """

    return os.path.join(buildable.build_dir,
            f'sip{module_name}part{part_nr}{source_suffix}')


def _composite_module_code(backend, sf, py_debug):
    """ Output the code for a composite module. """

    spec = backend.spec
    module = spec.module

    _declare_limited_api(sf, py_debug)
    _include_sip_h(sf, module)

    sf.write(
'''

static void sip_import_component_module(PyObject *d, const char *name)
{
    PyObject *mod;

    PyErr_Clear();

    mod = PyImport_ImportModule(name);

    /*
     * Note that we don't complain if the module can't be imported.  This
     * is a favour to Linux distro packagers who like to split PyQt into
     * different sub-packages.
     */
    if (mod)
    {
        PyDict_Merge(d, PyModule_GetDict(mod), 0);
        Py_DECREF(mod);
    }
}
''')

    backend.g_module_docstring(sf)
    backend.g_module_init_start(sf)
    backend.g_module_definition(sf)

    sf.write(
'''
    PyObject *sipModule, *sipModuleDict;

    if ((sipModule = PyModule_Create(&sip_module_def)) == SIP_NULLPTR)
        return SIP_NULLPTR;

    sipModuleDict = PyModule_GetDict(sipModule);

''')

    for mod in module.all_imports:
        sf.write(
f'    sip_import_component_module(sipModuleDict, "{mod.fq_py_name}");\n')

    sf.write(
'''
    PyErr_Clear();

    return sipModule;
}
''')


def _module_code(backend, bindings, project, py_debug, buildable):
    """ Generate the C/C++ code for a module. """

    spec = backend.spec
    module = spec.module
    module_name = module.py_name
    parts = bindings.concatenate

    source_suffix = bindings.source_suffix
    if source_suffix is None:
        source_suffix = '.c' if spec.c_bindings else '.cpp'

    # Calculate the number of files in each part.
    if parts:
        nr_files = 1

        for iface_file in spec.iface_files:
            if iface_file.module is module and iface_file.type is not IfaceFileType.EXCEPTION:
                nr_files += 1

        max_per_part = (nr_files + parts - 1) // parts
        files_in_part = 1
        this_part = 0

        source_name = _make_part_name(buildable, module_name, 0, source_suffix)
    else:
        source_name = os.path.join(buildable.build_dir,
                'sip' + module_name + 'cmodule' + source_suffix)

    sf = CompilationUnit(source_name, "Module code.", module, project,
            buildable)

    # Include the library headers for types used by virtual handlers, module
    # level functions, module level variables, exceptions and Qt meta types.
    _used_includes(sf, module.used)

    sf.write_code(module.unit_postinclude_code)

    # If there should be a Qt support API then generate stubs values for the
    # optional parts.  These should be undefined in %ModuleCode if a C++
    # implementation is provided.
    if spec.target_abi < (13, 0) and backend.module_supports_qt():
        sf.write(
'''
#define sipQtCreateUniversalSignal          0
#define sipQtFindUniversalSignal            0
#define sipQtEmitSignal                     0
#define sipQtConnectPySignal                0
#define sipQtDisconnectPySignal             0
''')

    # Only the legacy ABIs use a cached version of the module name.
    if spec.target_abi < (14, 0):
        module.fq_py_name.used = True

        # Transform the name cache.
        name_cache_list = _name_cache_as_list(spec.name_cache)

        # Define the names.
        has_sip_strings = _name_cache(sf, spec, name_cache_list)
    else:
        name_cache_list = None
        has_sip_strings = False

    # Generate the C++ code blocks.
    sf.write_code(module.module_code)

    # Generate any virtual handlers.
    for handler in spec.virtual_handlers:
        _virtual_handler(backend, sf, handler)

    # Generate any virtual error handlers.
    for virtual_error_handler in spec.virtual_error_handlers:
        if virtual_error_handler.module is module:
            self_name = _use_in_code(virtual_error_handler.code, 'sipPySelf')
            state_name = _use_in_code(virtual_error_handler.code,
                    'sipGILState')

            sf.write(
f'''

void sipVEH_{module_name}_{virtual_error_handler.name}(sipSimpleWrapper *{self_name}, sip_gilstate_t {state_name})
{{
''')

            sf.write_code(virtual_error_handler.code)

            sf.write('}\n')

    # Generate the global functions.
    slot_extenders = False

    for member in module.global_functions:
        if member.py_slot is None:
            _ordinary_function(backend, sf, bindings, member)
        else:
            # Make sure that there is still an overload and we haven't moved
            # them all to classes.
            for overload in module.overloads:
                if overload.common is member:
                    _py_slot(backend, sf, bindings, member)
                    slot_extenders = True
                    break

    # Generate the global functions for any hidden namespaces.
    for klass in spec.classes:
        if klass.iface_file.module is module and klass.is_hidden_namespace:
            for member in klass.members:
                if member.py_slot is None:
                    _ordinary_function(backend, sf, bindings, member,
                            scope=klass)

    # Generate any class specific __init__ or slot extenders.
    init_extenders = False

    for klass in module.proxies:
        if len(klass.ctors) != 0:
            _type_init(backend, sf, bindings, klass)
            init_extenders = True

        for member in klass.members:
            _py_slot(backend, sf, bindings, member, scope=klass)
            slot_extenders = True

    # Generate any __init__ extender table.
    if init_extenders:
        sf.write(
'''
static sipInitExtenderDef initExtenders[] = {
''')

        first_field = '-1, ' if spec.target_abi < (13, 0) else ''

        for klass in module.proxies:
            if len(klass.ctors) != 0:
                klass_name = klass.iface_file.fq_cpp_name.as_word
                encoded_type = get_encoded_type(module, klass)

                sf.write(f'    {{{first_field}init_type_{klass_name}, {encoded_type}, SIP_NULLPTR}},\n')

        sf.write(
f'''    {{{first_field}SIP_NULLPTR, {{0, 0, 0}}, SIP_NULLPTR}}
}};
''')

    # Generate any slot extender table.
    if slot_extenders:
        sf.write(
'''
static sipPySlotExtenderDef slotExtenders[] = {\n''')

        for member in module.global_functions:
            if member.py_slot is None:
                continue

            for overload in module.overloads:
                if overload.common is member:
                    member_name = member.py_name
                    slot_name = get_slot_name(member.py_slot)

                    sf.write(
f'    {{(void *)slot_{member_name}, {slot_name}, {{0, 0, 0}}}},\n')

                    break

        for klass in module.proxies:
            for member in klass.members:
                klass_name = klass.iface_file.fq_cpp_name.as_word
                member_name = member.py_name
                slot_name = get_slot_name(member.py_slot)
                encoded_type = get_encoded_type(module, klass)

                sf.write(f'    {{(void *)slot_{klass_name}_{member_name}, {slot_name}, {encoded_type}}},\n')

        sf.write(
'''    {SIP_NULLPTR, (sipPySlotType)0, {0, 0, 0}}
};
''')

    # Generate the global access functions.
    _access_functions(backend, sf)

    # Generate any sub-class convertors.
    nr_subclass_convertors = _subclass_convertors(sf, spec, module)

    # Generate the external classes table if needed.
    has_external = False

    for klass in spec.classes:
        if not klass.external:
            continue

        if klass.iface_file.module is not module:
            continue

        if not has_external:
            sf.write(
'''

/* This defines each external type declared in this module, */
static sipExternalTypeDef externalTypesTable[] = {
''')

            has_external = True

        type_nr = klass.iface_file.type_nr
        klass_py = klass.iface_file.fq_cpp_name.as_py

        sf.write(f'    {{{type_nr}, "{klass_py}"}},\n')

    if has_external:
        sf.write(
'''    {-1, SIP_NULLPTR}
};
''')

    # Generate any enum slot tables.
    for enum in spec.enums:
        if enum.module is not module or enum.fq_cpp_name is None:
            continue

        if len(enum.slots) == 0:
            continue

        for member in enum.slots:
            _py_slot(backend, sf, bindings, member, scope=enum)

        enum_name = enum.fq_cpp_name.as_word

        sf.write(
f'''
static sipPySlotDef slots_{enum_name}[] = {{
''')

        for member in enum.slots:
            if member.py_slot is not None:
                member_name = member.py_name
                slot_name = get_slot_name(member.py_slot)

                sf.write(f'    {{(void *)slot_{enum_name}_{member_name}, {slot_name}}},\n')

        sf.write(
'''    {SIP_NULLPTR, (sipPySlotType)0}
};

''')

    # Generate the enum type structures while recording the order in which they
    # are generated.  Note that we go through the sorted table of needed types
    # rather than the unsorted list of all enums.
    needed_enums = []

    # TODO ABI v14 only wants these for the local module.  Other ABIs as well?
    for needed_type in module.needed_types:
        if needed_type.type is not ArgumentType.ENUM:
            continue

        enum = needed_type.definition

        scope_type_nr = -1 if enum.scope is None else enum.scope.iface_file.type_nr

        if len(needed_enums) == 0:
            sf.write('static sipEnumTypeDef enumTypes[] = {\n')

        cpp_name = get_normalised_cached_name(enum.cached_fq_cpp_name)
        py_name = get_normalised_cached_name(enum.py_name)

        if backend.py_enums_supported():
            base_type = 'SIP_ENUM_' + enum.base_type.name
            nr_members = len(enum.members)

            sf.write(
f'    {{{{SIP_NULLPTR, SIP_TYPE_ENUM, sipNameNr_{cpp_name}, SIP_NULLPTR, 0}}, {base_type}, sipNameNr_{py_name}, {scope_type_nr}, {nr_members}')
        else:
            sip_type = 'SIP_TYPE_SCOPED_ENUM' if enum.is_scoped else 'SIP_TYPE_ENUM'

            v12_fields = '-1, SIP_NULLPTR, ' if spec.target_abi < (13, 0)  else ''

            sf.write(
f'    {{{{{v12_fields}SIP_NULLPTR, {sip_type}, sipNameNr_{cpp_name}, SIP_NULLPTR, 0}}, sipNameNr_{py_name}, {scope_type_nr}')

        if len(enum.slots) == 0:
            sf.write(', SIP_NULLPTR')
        else:
            sf.write(', slots_' + enum.fq_cpp_name.as_word)

        sf.write('},\n')

        needed_enums.append(enum)

    if len(needed_enums) != 0:
        sf.write('};\n')

    if backend.custom_enums_supported():
        nr_enum_members = backend.g_enum_member_table(sf)
    else:
        nr_enum_members = -1

    # Generate the types table.
    if len(module.needed_types) != 0:
        backend.g_types_table(sf, module, needed_enums)

    # Generate the typedefs table.
    if module.nr_typedefs > 0:
        sf.write(
'''

/*
 * These define each typedef in this module.
 */
static sipTypedefDef typedefsTable[] = {
''')

        for typedef in spec.typedefs:
            if typedef.module is not module:
                continue

            cpp_name = typedef.fq_cpp_name.cpp_stripped(STRIP_GLOBAL)

            sf.write(f'    {{"{cpp_name}", "')

            # The default behaviour isn't right in a couple of cases.
            # TODO: is this still true?
            if typedef.type.type is ArgumentType.LONGLONG:
                sf.write('long long')
            elif typedef.type.type is ArgumentType.ULONGLONG:
                sf.write('unsigned long long')
            else:
                sf.write(
                        fmt_argument_as_cpp_type(spec, typedef.type,
                                strip=STRIP_GLOBAL, use_typename=False))

            sf.write('"},\n')

        sf.write('};\n')

    # Generate the error handlers table.
    has_virtual_error_handlers = False

    for handler in spec.virtual_error_handlers:
        if handler.module is not module:
            continue

        if not has_virtual_error_handlers:
            has_virtual_error_handlers = True

            sf.write(
'''

/*
 * This defines the virtual error handlers that this module implements and
 * can be used by other modules.
 */
static sipVirtErrorHandlerDef virtErrorHandlersTable[] = {
''')

        sf.write(f'    {{"{handler.name}", sipVEH_{module_name}_{handler.name}}},\n')

    if has_virtual_error_handlers:
        sf.write(
'''    {SIP_NULLPTR, SIP_NULLPTR}
};
''')

    # Generate the tables for things we are importing.
    for imported_module in module.all_imports:
        imported_module_name = imported_module.py_name

        if len(imported_module.needed_types) != 0:
            sf.write(
f'''

/* This defines the types that this module needs to import from {imported_module_name}. */
sipImportedTypeDef sipImportedTypes_{module_name}_{imported_module_name}[] = {{
''')

            for needed_type in imported_module.needed_types:
                if needed_type.type is ArgumentType.MAPPED:
                    type_name = needed_type.definition.cpp_name
                else:
                    if needed_type.type is ArgumentType.CLASS:
                        scoped_name = needed_type.definition.iface_file.fq_cpp_name
                    else:
                        scoped_name = needed_type.definition.fq_cpp_name

                    type_name = scoped_name.cpp_stripped(STRIP_GLOBAL)

                sf.write(f'    {{"{type_name}"}},\n')

            sf.write(
'''    {SIP_NULLPTR}
};
''')

        if imported_module.nr_virtual_error_handlers > 0:
            sf.write(
f'''

/*
 * This defines the virtual error handlers that this module needs to import
 * from {imported_module_name}.
 */
sipImportedVirtErrorHandlerDef sipImportedVirtErrorHandlers_{module_name}_{imported_module_name}[] = {{
''')

            # The handlers are unordered so search for each in turn.  There
            # will probably be only one so speed isn't an issue.
            for i in range(imported_module.nr_virtual_error_handlers):
                for handler in spec.virtual_error_handlers:
                    if handler.module is imported_module and handler.handler_nr == i:
                        sf.write(f'    {{"{handler.name}"}},\n')

            sf.write(
'''    {SIP_NULLPTR}
};
''')

        if imported_module.nr_exceptions > 0:
            sf.write(
f'''

/*
 * This defines the exception objects that this module needs to import from
 * {imported_module_name}.
 */
sipImportedExceptionDef sipImportedExceptions_{module_name}_{imported_module_name}[] = {{
''')

            # The exceptions are unordered so search for each in turn.  There
            # will probably be very few so speed isn't an issue.
            for i in range(imported_module.nr_exceptions):
                for exception in spec.exceptions:
                    if exception.iface_file.module is imported_module and exception.exception_nr == i:
                        sf.write(f'    {{"{exception.py_name}"}},\n')

            sf.write(
'''    {SIP_NULLPTR}
};
''')

    if len(module.all_imports) != 0:
        sf.write(
'''

/* This defines the modules that this module needs to import. */
static sipImportedModuleDef importsTable[] = {
''')

        for imported_module in module.all_imports:
            imported_module_name = imported_module.py_name

            types = handlers = exceptions = 'SIP_NULLPTR'

            if len(imported_module.needed_types) != 0:
                types = f'sipImportedTypes_{module_name}_{imported_module_name}'

            if imported_module.nr_virtual_error_handlers != 0:
                handlers = f'sipImportedVirtErrorHandlers_{module_name}_{imported_module_name}'

            if imported_module.nr_exceptions != 0:
                exceptions = f'sipImportedExceptions_{module_name}_{imported_module_name}'

            sf.write(f'    {{"{imported_module.fq_py_name}", {types}, {handlers}, {exceptions}}},\n')

        sf.write(
'''    {SIP_NULLPTR, SIP_NULLPTR, SIP_NULLPTR, SIP_NULLPTR}
};
''')

    # Generate the table of sub-class convertors
    if nr_subclass_convertors > 0:
        sf.write(
'''

/* This defines the class sub-convertors that this module defines. */
static sipSubClassConvertorDef convertorsTable[] = {
''')

        for klass in spec.classes:
            if klass.iface_file.module is not module:
                continue

            if klass.convert_to_subclass_code is None:
                continue

            klass_name = klass.iface_file.fq_cpp_name.as_word
            encoded_type = get_encoded_type(module, klass.subclass_base)

            sf.write(f'    {{sipSubClass_{klass_name}, {encoded_type}, SIP_NULLPTR}},\n')

        sf.write(
'''    {SIP_NULLPTR, {0, 0, 0}, SIP_NULLPTR}
};
''')

    # Generate any license information.
    if module.license is not None:
        license = module.license
        licensee = 'SIP_NULLPTR' if license.licensee is None else '"' + license.licensee + '"'
        timestamp = 'SIP_NULLPTR' if license.timestamp is None else '"' + license.timestamp + '"'
        signature = 'SIP_NULLPTR' if license.signature is None else '"' + license.signature + '"'

        sf.write(
f'''

/* Define the module's license. */
static sipLicenseDef module_license = {{
    "{license.type}",
    {licensee},
    {timestamp},
    {signature}
}};
''')

    # Generate static variables table for the module.
    static_variables_state = backend.g_static_variables_table(sf)

    # Generate any exceptions support.
    if bindings.exceptions:
        if module.nr_exceptions > 0:
            sf.write(
f'''

PyObject *sipExportedExceptions_{module_name}[{module.nr_exceptions + 1}];
''')

        if backend.abi_has_next_exception_handler():
            _exception_handler(backend, sf)

    # Generate any Qt support API.
    if spec.target_abi < (13, 0) and backend.module_supports_qt():
        sf.write(
f'''

/* This defines the Qt support API. */

static sipQtAPI qtAPI = {{
    &sipExportedTypes_{module_name}[{spec.pyqt_qobject.iface_file.type_nr}],
    sipQtCreateUniversalSignal,
    sipQtFindUniversalSignal,
    sipQtCreateUniversalSlot,
    sipQtDestroyUniversalSlot,
    sipQtFindSlot,
    sipQtConnect,
    sipQtDisconnect,
    sipQtSameSignalSlotName,
    sipQtFindSipslot,
    sipQtEmitSignal,
    sipQtConnectPySignal,
    sipQtDisconnectPySignal
}};
''')

    sf.write('\n\n')

    # Generate the code to create the wrapped module
    backend.g_create_wrapped_module(sf, bindings,
        has_sip_strings,
        has_external,
        nr_enum_members,
        has_virtual_error_handlers,
        nr_subclass_convertors,
        static_variables_state,
        slot_extenders,
        init_extenders
    )

    # Generate the interface source files.
    for iface_file in spec.iface_files:
        if iface_file.module is module and iface_file.type is not IfaceFileType.EXCEPTION:
            need_postinc = False
            use_sf = None

            if parts:
                if files_in_part == max_per_part:
                    # Close the old part.
                    sf.close()

                    # Create a new one.
                    files_in_part = 1
                    this_part += 1

                    source_name = _make_part_name(buildable, module_name,
                            this_part, source_suffix)
                    sf = CompilationUnit(source_name, "Module code.", module,
                            project, buildable)

                    need_postinc = True
                else:
                    files_in_part += 1

                if iface_file.file_extension is None:
                    # The interface file should use this source file rather
                    # than create one of its own.
                    use_sf = sf

            _iface_file_cpp(backend, use_sf, bindings, project, buildable,
                    py_debug, iface_file, need_postinc, source_suffix)

    sf.close()

    header_name = os.path.join(buildable.build_dir, f'sipAPI{module_name}.h')

    with SourceFile(header_name, "Internal module API header file.", module, project, buildable.headers) as sf:
        _internal_api_header(backend, sf, bindings, py_debug,
                name_cache_list if has_sip_strings else None)


def _name_cache_as_list(name_cache):
    """ Return a name cache as a correctly ordered list of CachedName objects.
    """

    name_cache_list = []

    # Create the list sorted first by descending name length and then
    # alphabetical order.
    for k in sorted(name_cache.keys(), reverse=True):
        name_cache_list.extend(sorted(name_cache[k], key=lambda k: k.name))

    # Set the offset into the string pool for every used name.
    offset = 0

    for cached_name in name_cache_list:
        if not cached_name.used:
            continue

        name_len = len(cached_name.name)

        # See if the tail of a previous used name could be used instead.
        for prev_name in name_cache_list:
            prev_name_len = len(prev_name.name)

            if prev_name_len <= name_len:
                break

            if not prev_name.used or prev_name.is_substring:
                continue

            if prev_name.name.endswith(cached_name.name):
                cached_name.is_substring = True
                cached_name.offset = prev_name.offset + prev_name_len - name_len;
                break

        if not cached_name.is_substring:
            cached_name.offset = offset
            offset += name_len + 1

    return name_cache_list


def _name_cache(sf, spec, name_cache_list):
    """ Generate the name cache definition.  Return True if something was
    actually generated.
    """

    has_sip_strings = False

    for name in name_cache_list:
        if not name.used or name.is_substring:
            continue

        if not has_sip_strings:
            has_sip_strings = True

            sf.write(
f'''
/* Define the strings used by this module. */
const char sipStrings_{spec.module.py_name}[] = {{
''')

        sf.write('    ')

        for ch in name.name:
            sf.write(f"'{ch}', ")

        sf.write('0,\n')

    if has_sip_strings:
        sf.write('};\n')

    return has_sip_strings


def _subclass_convertors(sf, spec, module):
    """ Generate all the sub-class convertors for a module and return the
    number of them.
    """

    nr_subclass_convertors = 0

    for klass in spec.classes:
        if klass.iface_file.module is not module:
            continue

        if klass.convert_to_subclass_code is None:
            continue

        sf.write(
'''

/* Convert to a sub-class if possible. */
''')

        klass_name = klass.iface_file.fq_cpp_name.as_word
        base_cpp = klass.subclass_base.iface_file.fq_cpp_name.as_cpp

        if not spec.c_bindings:
            sf.write(
f'extern "C" {{static const sipTypeDef *sipSubClass_{klass_name}(void **);}}\n')

        # Allow the deprecated use of sipClass rather than sipType.
        if is_used_in_code(klass.convert_to_subclass_code, 'sipClass'):
            decl = 'sipWrapperType *sipClass'
            result = '(sipClass ? sipClass->wt_td : 0)'
        else:
            decl = 'const sipTypeDef *sipType'
            result = 'sipType'

        sf.write(
f'''static const sipTypeDef *sipSubClass_{klass_name}(void **sipCppRet)
{{
    {base_cpp} *sipCpp = reinterpret_cast<{base_cpp} *>(*sipCppRet);
    {decl};

''')

        sf.write_code(klass.convert_to_subclass_code)

        sf.write(
f'''
    return {result};
}}
''')

        nr_subclass_convertors += 1

    return nr_subclass_convertors


def _ordinary_function(backend, sf, bindings, member, scope=None):
    """ Generate an ordinary function. """

    spec = backend.spec
    member_name = member.py_name.name

    if scope is None:
        overloads = spec.module.overloads
        py_scope = None
        py_scope_prefix = ''
    else:
        overloads = scope.overloads
        py_scope = py_scope(scope)

        if py_scope is not None:
            member_name = py_scope.iface_file.fq_cpp_name.as_word + '_' + member_name

    sf.write('\n\n')

    # Generate the docstrings.
    if has_method_docstring(bindings, member, overloads):
        sf.write(f'PyDoc_STRVAR(doc_{member_name}, "')
        has_auto_docstring = backend.g_method_docstring(sf, bindings, member,
                overloads)
        sf.write('");\n\n')
    else:
        has_auto_docstring = False

    if member.no_arg_parser or member.allow_keyword_args:
        kw_fw_decl = ', PyObject *'
        kw_decl = ', PyObject *sipKwds'
    else:
        kw_fw_decl = kw_decl = ''

    if py_scope is None:
        if not spec.c_bindings:
            sf.write(f'extern "C" {{static PyObject *func_{member_name}({backend.py_method_args(is_impl=False, is_method=False)}{kw_fw_decl});}}\n')

        sf.write(f'static PyObject *func_{member_name}({backend.py_method_args(is_impl=True, is_method=False)}{kw_decl})\n')
    else:
        if not spec.c_bindings:
            sf.write(f'extern "C" {{static PyObject *meth_{member_name}({backend.py_method_args(is_impl=False, is_method=True)}{kw_fw_decl});}}\n')

        sf.write(f'static PyObject *meth_{member_name}({backend.py_method_args(is_impl=True, is_method=True)}{kw_decl})\n')

    sf.write('{\n')

    need_intro = True

    for overload in overloads:
        if overload.common is not member:
            continue

        if member.no_arg_parser:
            sf.write_code(overload.method_code)
            break

        if need_intro:
            if py_scope is None:
                backend.g_function_support_vars(sf)
            else:
                backend.g_method_support_vars(sf)

            sf.write('    PyObject *sipParseErr = SIP_NULLPTR;\n')

            if py_scope is None and spec.c_bindings:
                sf.write(
'''
    (void)sipSelf;
''')

            need_intro = False

        _function_body(backend, sf, bindings, scope, overload)

    if not need_intro:
        sf.write(
f'''
    /* Raise an exception if the arguments couldn't be parsed. */
    sipNoFunction(sipParseErr, {backend.cached_name_ref(member.py_name)}, ''')

        if has_auto_docstring:
            sf.write(f'doc_{member_name}')
        else:
            sf.write('SIP_NULLPTR')

        sf.write(''');

    return SIP_NULLPTR;
''')

    sf.write('}\n')


def _access_functions(backend, sf, scope=None):
    """ Generate the access functions for the variables. """

    spec = backend.spec

    for variable in backend.variables_in_scope(scope, check_handler=False):
        if variable.access_code is None:
            continue

        cpp_name = variable.fq_cpp_name.as_word

        sf.write('\n\n/* Access function. */\n')

        if not spec.c_bindings:
            sf.write(f'extern "C" {{static void *access_{cpp_name}();}}\n')

        sf.write(f'static void *access_{cpp_name}()\n{{\n')
        sf.write_code(variable.access_code)
        sf.write('}\n')


def _empty_iface_file(spec, iface_file):
    """ See if an interface file has any content. """

    for klass in spec.classes:
        if klass.iface_file is iface_file and not klass.is_hidden_namespace and not klass.is_protected and not klass.external:
            return False

    for mapped_type in spec.mapped_types:
        if mapped_type.iface_file is iface_file:
            return False

    return True


def _iface_file_cpp(backend, sf, bindings, project, buildable, py_debug,
        iface_file, need_postinc, source_suffix):
    """ Generate the C/C++ code for an interface. """

    spec = backend.spec

    # Check that there will be something in the file so that we don't get
    # warning messages from ranlib.
    if _empty_iface_file(spec, iface_file):
        return

    if sf is None:
        source_name = os.path.join(buildable.build_dir,
                'sip' + iface_file.module.py_name)

        for part in iface_file.fq_cpp_name:
            source_name += part

        if iface_file.file_extension is not None:
            source_suffix = iface_file.file_extension

        source_name += source_suffix

        sf = CompilationUnit(source_name, "Interface wrapper code.",
                iface_file.module, project, buildable)

        need_postinc = True

    sf.write('\n')

    sf.write_code(iface_file.type_header_code)
    _used_includes(sf, iface_file.used)

    if need_postinc:
        sf.write_code(iface_file.module.unit_postinclude_code)

    for klass in spec.classes:
        # Protected classes must be generated in the interface file of the
        # enclosing scope.
        if klass.is_protected:
            continue

        if klass.external:
            continue

        if klass.iface_file is iface_file:
            _class_cpp(backend, sf, bindings, klass, py_debug)

            # Generate any enclosed protected classes.
            for proto_klass in spec.classes:
                if proto_klass.is_protected and proto_klass.scope is klass:
                    _class_cpp(backend, sf, bindings, proto_klass, py_debug)

    for mapped_type in spec.mapped_types:
        if mapped_type.iface_file is iface_file:
            _mapped_type_cpp(backend, sf, bindings, mapped_type)


def _mapped_type_cpp(backend, sf, bindings, mapped_type):
    """ Generate the C++ code for a mapped type version. """

    spec = backend.spec
    mapped_type_name = mapped_type.iface_file.fq_cpp_name.as_word
    mapped_type_type = fmt_argument_as_cpp_type(spec, mapped_type.type,
            plain=True, no_derefs=True)

    sf.write_code(mapped_type.type_code)

    if not mapped_type.no_release:
        # Generate the assignment helper.  Note that the source pointer is not
        # const.  This is to allow the source instance to be modified as a
        # consequence of the assignment, eg. if it is implementing some sort of
        # reference counting scheme.
        if not mapped_type.no_assignment_operator:
            sf.write('\n\n')

            if not spec.c_bindings:
                sf.write(f'extern "C" {{static void assign_{mapped_type_name}(void *, Py_ssize_t, void *);}}\n')

            sf.write(f'static void assign_{mapped_type_name}(void *sipDst, Py_ssize_t sipDstIdx, void *sipSrc)\n{{\n')

            if spec.c_bindings:
                sf.write(f'    (({mapped_type_type} *)sipDst)[sipDstIdx] = *(({mapped_type_type} *)sipSrc);\n')
            else:
                sf.write(f'    reinterpret_cast<{mapped_type_type} *>(sipDst)[sipDstIdx] = *reinterpret_cast<{mapped_type_type} *>(sipSrc);\n')

            sf.write('}\n')

        # Generate the array allocation helper.
        if not mapped_type.no_default_ctor:
            sf.write('\n\n')

            if not spec.c_bindings:
                sf.write(f'extern "C" {{static void *array_{mapped_type_name}(Py_ssize_t);}}\n')

            sf.write(f'static void *array_{mapped_type_name}(Py_ssize_t sipNrElem)\n{{\n')

            if spec.c_bindings:
                sf.write(f'    return sipMalloc(sizeof ({mapped_type_type}) * sipNrElem);\n')
            else:
                sf.write(f'    return new {mapped_type_type}[sipNrElem];\n')

            sf.write('}\n')

        # Generate the copy helper.
        if not mapped_type.no_copy_ctor:
            sf.write('\n\n')

            if not spec.c_bindings:
                sf.write(f'extern "C" {{static void *copy_{mapped_type_name}(const void *, Py_ssize_t);}}\n')

            sf.write(f'static void *copy_{mapped_type_name}(const void *sipSrc, Py_ssize_t sipSrcIdx)\n{{\n')

            if spec.c_bindings:
                sf.write(
f'''    {mapped_type_type} *sipPtr = sipMalloc(sizeof ({mapped_type_type}));
    *sipPtr = ((const {mapped_type_type} *)sipSrc)[sipSrcIdx];

    return sipPtr;
''')
            else:
                sf.write(f'    return new {mapped_type_type}(reinterpret_cast<const {mapped_type_type} *>(sipSrc)[sipSrcIdx]);\n')

            sf.write('}\n')

        sf.write('\n\n/* Call the mapped type\'s destructor. */\n')

        need_state = is_used_in_code(mapped_type.release_code, 'sipState')

        if not spec.c_bindings:
            arg_3 = ', void *' if spec.target_abi >= (13, 0) else ''
            sf.write(f'extern "C" {{static void release_{mapped_type_name}(void *, int{arg_3});}}\n')

        arg_2 = ' sipState' if spec.c_bindings or need_state else ''
        sf.write(f'static void release_{mapped_type_name}(void *sipCppV, int{arg_2}')

        if spec.target_abi >= (13, 0):
            user_state = _use_in_code(mapped_type.release_code, 'sipUserState')
            sf.write(', void *' + user_state)

        sf.write(f')\n{{\n    {_mapped_type_from_void(spec, mapped_type_type)};\n')

        if bindings.release_gil:
            sf.write('    Py_BEGIN_ALLOW_THREADS\n')

        if mapped_type.release_code is not None:
            sf.write_code(mapped_type.release_code)
        elif spec.c_bindings:
            sf.write('    sipFree(sipCpp);\n')
        else:
            sf.write('    delete sipCpp;\n')

        if bindings.release_gil:
            sf.write('    Py_END_ALLOW_THREADS\n')

        sf.write('}\n\n')

    _convert_to_definitions(sf, spec, mapped_type)

    # Generate the from type convertor.
    if mapped_type.convert_from_type_code is not None:
        sf.write('\n\n')

        if not spec.c_bindings:
            sf.write(f'extern "C" {{static PyObject *convertFrom_{mapped_type_name}(void *, PyObject *);}}\n')

        xfer = _use_in_code(mapped_type.convert_from_type_code,
                'sipTransferObj', spec=spec)

        sf.write(
f'''static PyObject *convertFrom_{mapped_type_name}(void *sipCppV, PyObject *{xfer})
{{
    {_mapped_type_from_void(spec, mapped_type_type)};

''')

        sf.write_code(mapped_type.convert_from_type_code)

        sf.write('}\n')

    # Generate the static methods.
    for member in mapped_type.members:
        _ordinary_function(backend, sf, bindings, member, scope=mapped_type)

    cod_nrmethods = backend.g_mapped_type_method_table(sf, bindings,
            mapped_type)

    id_int = 'SIP_NULLPTR'

    if backend.custom_enums_supported():
        cod_nrenummembers = backend.g_enum_member_table(sf, scope=mapped_type)
        has_ints = False
        needs_namespace = (cod_nrenummembers > 0)
    else:
        if _int_instances(backend, sf, scope=mapped_type):
            id_int = 'intInstances_' + mapped_type_name

        needs_namespace = False

    if cod_nrmethods > 0:
        needs_namespace = True

    if backend.pyqt6_supported() and mapped_type.pyqt_flags != 0:
        sf.write(f'\n\nstatic pyqt6MappedTypePluginDef plugin_{mapped_type_name} = {{{mapped_type.pyqt_flags}}};\n')

        td_plugin_data = '&plugin_' + mapped_type_name
    else:
        td_plugin_data = 'SIP_NULLPTR'

    sf.write(
f'''

sipMappedTypeDef sipTypeDef_{mapped_type.iface_file.module.py_name}_{mapped_type_name} = {{
    {{
''')

    if spec.target_abi < (13, 0):
        sf.write(
'''        -1,
        SIP_NULLPTR,
''')

    flags = []

    if mapped_type.handles_none:
        flags.append('SIP_TYPE_ALLOW_NONE')

    if mapped_type.needs_user_state:
        flags.append('SIP_TYPE_USER_STATE')

    flags.append('SIP_TYPE_MAPPED')

    td_flags = '|'.join(flags)

    td_cname = backend.cached_name_ref(mapped_type.cpp_name, as_nr=True)

    cod_name = backend.cached_name_ref(mapped_type.py_name, as_nr=True) if needs_namespace else '-1'
    cod_methods = 'SIP_NULLPTR' if cod_nrmethods == 0 else 'methods_' + mapped_type_name

    sf.write(
f'''        SIP_NULLPTR,
        {td_flags},
        {td_cname},
        SIP_NULLPTR,
        {td_plugin_data},
    }},
    {{
        {cod_name},
        {{0, 0, 1}},
        {cod_nrmethods}, {cod_methods},
''')

    if backend.custom_enums_supported():
        cod_enummembers = 'SIP_NULLPTR' if cod_nrenummembers == 0 else 'enummembers_' + mapped_type_name

        sf.write(
f'''        {cod_nrenummembers}, {cod_enummembers},
''')

    mtd_assign = 'SIP_NULLPTR' if mapped_type.no_assignment_operator else 'assign_' + mapped_type_name
    mtd_array = 'SIP_NULLPTR' if mapped_type.no_default_ctor else 'array_' + mapped_type_name
    mtd_copy = 'SIP_NULLPTR' if mapped_type.no_copy_ctor else 'copy_' + mapped_type_name
    mtd_release = 'SIP_NULLPTR' if mapped_type.no_release else 'release_' + mapped_type_name
    mtd_cto = 'SIP_NULLPTR' if mapped_type.convert_to_type_code is None else 'convertTo_' + mapped_type_name
    mtd_cfrom = 'SIP_NULLPTR' if mapped_type.convert_from_type_code is None else 'convertFrom_' + mapped_type_name

    sf.write(
f'''        0, SIP_NULLPTR,
        {{SIP_NULLPTR, SIP_NULLPTR, SIP_NULLPTR, SIP_NULLPTR, {id_int}, SIP_NULLPTR, SIP_NULLPTR, SIP_NULLPTR, SIP_NULLPTR, SIP_NULLPTR}}
    }},
    {mtd_assign},
    {mtd_array},
    {mtd_copy},
    {mtd_release},
    {mtd_cto},
    {mtd_cfrom}
}};
''')


def _class_cpp(backend, sf, bindings, klass, py_debug):
    """ Generate the C++ code for a class. """

    spec = backend.spec

    sf.write_code(klass.type_code)
    _class_functions(backend, sf, bindings, klass, py_debug)
    _access_functions(backend, sf, scope=klass)

    if klass.iface_file.type is not IfaceFileType.NAMESPACE:
        _convert_to_definitions(sf, spec, klass)

        # Generate the optional from type convertor.
        if klass.convert_from_type_code is not None:
            sf.write('\n\n')

            name = klass.iface_file.fq_cpp_name.as_word
            xfer = _use_in_code(klass.convert_from_type_code, 'sipTransferObj',
                    spec=spec)

            if not spec.c_bindings:
                sf.write(f'extern "C" {{static PyObject *convertFrom_{name}(void *, PyObject *);}}\n')

            sf.write(
f'''static PyObject *convertFrom_{name}(void *sipCppV, PyObject *{xfer})
{{
    {_class_from_void(backend, klass)};

''')

            sf.write_code(klass.convert_from_type_code)

            sf.write('}\n')

    backend.g_type_definition(sf, bindings, klass, py_debug)


def _convert_to_definitions(sf, spec, scope):
    """ Generate the "to type" convertor definitions. """

    convert_to_type_code = scope.convert_to_type_code

    if convert_to_type_code is None:
        return

    scope_type = Argument(
            ArgumentType.CLASS if isinstance(scope, WrappedClass) else ArgumentType.MAPPED,
            definition=scope)

    # Sometimes type convertors are just stubs that set the error flag, so
    # check if we actually need everything so that we can avoid compiler
    # warnings.
    sip_py = _use_in_code(convert_to_type_code, 'sipPy', spec=spec)
    sip_cpp_ptr = _use_in_code(convert_to_type_code, 'sipCppPtr', spec=spec)
    sip_is_err = _use_in_code(convert_to_type_code, 'sipIsErr', spec=spec)
    xfer = _use_in_code(convert_to_type_code, 'sipTransferObj', spec=spec)

    if spec.target_abi >= (13, 0):
        need_us_arg = True
        need_us_val = (spec.c_bindings or type_needs_user_state(scope_type))
    else:
        need_us_arg = False
        need_us_val = False

    scope_name = scope.iface_file.fq_cpp_name.as_word

    sf.write('\n\n')

    if not spec.c_bindings:
        sf.write(f'extern "C" {{static int convertTo_{scope_name}(PyObject *, void **, int *, PyObject *')

        if need_us_arg:
            sf.write(', void **')

        sf.write(');}\n')

    sip_cpp_ptr_v = sip_cpp_ptr
    if sip_cpp_ptr_v != '':
        sip_cpp_ptr_v += 'V'

    sf.write(f'static int convertTo_{scope_name}(PyObject *{sip_py}, void **{sip_cpp_ptr_v}, int *{sip_is_err}, PyObject *{xfer}')

    if need_us_arg:
        sf.write(', void **')

        if need_us_val:
            sf.write('sipUserStatePtr')

    sf.write(')\n{\n')

    if sip_cpp_ptr != '':
        type_s = fmt_argument_as_cpp_type(spec, scope_type, plain=True,
                no_derefs=True)

        if spec.c_bindings:
            cast_value = f'({type_s} **)sipCppPtrV'
        else:
            cast_value = f'reinterpret_cast<{type_s} **>(sipCppPtrV)'

        sf.write(f'    {type_s} **sipCppPtr = {cast_value};\n\n')

    sf.write_code(convert_to_type_code)

    sf.write('}\n')


def _py_slot(backend, sf, bindings, member, scope=None):
    """ Generate a Python slot handler for either a class, an enum or an
    extender.
    """

    spec = backend.spec

    if scope is None:
        prefix = ''
        py_name = None
        fq_cpp_name = None
        overloads = spec.module.overloads
    elif isinstance(scope, WrappedEnum):
        prefix = 'Type'
        py_name = scope.py_name
        fq_cpp_name = scope.fq_cpp_name
        overloads = scope.overloads
    else:
        prefix = 'Type'
        py_name = scope.py_name
        fq_cpp_name = scope.iface_file.fq_cpp_name
        overloads = scope.overloads

    if is_void_return_slot(member.py_slot) or is_int_return_slot(member.py_slot):
        ret_type = 'int '
        ret_value = '-1'
    elif is_ssize_return_slot(member.py_slot):
        ret_type = 'Py_ssize_t '
        ret_value = '0'
    elif is_hash_return_slot(member.py_slot):
        if spec.target_abi >= (13, 0):
            ret_type = 'Py_hash_t '
            ret_value = '0'
        else:
            ret_type = 'long '
            ret_value = '0L'
    else:
        ret_type = 'PyObject *'
        ret_value = 'SIP_NULLPTR'

    has_args = True

    if is_int_arg_slot(member.py_slot):
        has_args = False
        arg_str = 'PyObject *sipSelf, int a0'
        decl_arg_str = 'PyObject *, int'
    elif member.py_slot is PySlot.CALL:
        if spec.c_bindings or member.allow_keyword_args or member.no_arg_parser:
            arg_str = 'PyObject *sipSelf, PyObject *sipArgs, PyObject *sipKwds'
        else:
            arg_str = 'PyObject *sipSelf, PyObject *sipArgs, PyObject *'

        decl_arg_str = 'PyObject *, PyObject *, PyObject *'
    elif is_multi_arg_slot(member.py_slot):
        arg_str = 'PyObject *sipSelf, PyObject *sipArgs'
        decl_arg_str = 'PyObject *, PyObject *'
    elif is_zero_arg_slot(member.py_slot):
        has_args = False
        arg_str = 'PyObject *sipSelf'
        decl_arg_str = 'PyObject *'
    elif is_number_slot(member.py_slot):
        arg_str = 'PyObject *sipArg0, PyObject *sipArg1'
        decl_arg_str = 'PyObject *, PyObject *'
    elif member.py_slot is PySlot.SETATTR:
        arg_str = 'PyObject *sipSelf, PyObject *sipName, PyObject *sipValue'
        decl_arg_str = 'PyObject *, PyObject *, PyObject *'
    else:
        arg_str = 'PyObject *sipSelf, PyObject *sipArg'
        decl_arg_str = 'PyObject *, PyObject *'

    sf.write('\n\n')

    slot_decl = f'static {ret_type}slot_'

    if fq_cpp_name is not None:
        slot_decl += fq_cpp_name.as_word + '_'

    if not spec.c_bindings:
        sf.write(f'extern "C" {{{slot_decl}{member.py_name.name}({decl_arg_str});}}\n')

    sf.write(f'{slot_decl}{member.py_name.name}({arg_str})\n{{\n')

    if member.py_slot is PySlot.CALL and member.no_arg_parser:
        for overload in overloads:
            if overload.common is member:
                sf.write_code(overload.method_code)
    else:
        if is_inplace_number_slot(member.py_slot):
            sf.write(
f'''    if (!PyObject_TypeCheck(sipSelf, sipTypeAsPyTypeObject(sip{prefix}_{fq_cpp_name.as_word})))
    {{
        Py_INCREF(Py_NotImplemented);
        return Py_NotImplemented;
    }}

''')

        if not is_number_slot(member.py_slot):
            type_ref = backend.get_type_ref(scope)

            if isinstance(scope, WrappedClass):
                cpp_name = backend.scoped_class_name(scope)
                sf.write(
f'''    {cpp_name} *sipCpp = reinterpret_cast<{cpp_name} *>(sipGetCppPtr((sipSimpleWrapper *)sipSelf, {type_ref}));

    if (!sipCpp)
''')
            else:
                cpp_name = fq_cpp_name.as_cpp
                sf.write(
f'''    {cpp_name} sipCpp = static_cast<{cpp_name}>(sipConvertToEnum(sipSelf, {type_ref}));

    if (PyErr_Occurred())
''')

            sf.write(f'        return {ret_value};\n\n')

        if has_args:
            sf.write('    PyObject *sipParseErr = SIP_NULLPTR;\n')

        for overload in overloads:
            if overload.common is member and overload.is_abstract:
                sf.write('    PyObject *sipOrigSelf = sipSelf;\n')
                break

        scope_not_enum = not isinstance(scope, WrappedEnum)

        for overload in overloads:
            if overload.common is member:
                dereferenced = scope_not_enum and not overload.dont_deref_self

                _function_body(backend, sf, bindings, scope, overload,
                        dereferenced=dereferenced)

        if has_args:
            if member.py_slot in (PySlot.CONCAT, PySlot.ICONCAT, PySlot.REPEAT, PySlot.IREPEAT):
                sf.write(
f'''
    /* Raise an exception if the argument couldn't be parsed. */
    sipBadOperatorArg(sipSelf, sipArg, {get_slot_name(member.py_slot)});

    return SIP_NULLPTR;
''')

            else:
                if is_rich_compare_slot(member.py_slot):
                    sf.write(
'''
    Py_XDECREF(sipParseErr);
''')
                elif is_number_slot(member.py_slot) or is_inplace_number_slot(member.py_slot):
                    sf.write(
'''
    Py_XDECREF(sipParseErr);

    if (sipParseErr == Py_None)
        return SIP_NULLPTR;
''')

                if is_number_slot(member.py_slot) or is_rich_compare_slot(member.py_slot):
                    # We can only extend class slots. */
                    if not isinstance(scope, WrappedClass):
                        sf.write(
'''
    PyErr_Clear();

    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
''')
                    elif is_number_slot(member.py_slot):
                        sf.write(
f'''
    return sipPySlotExtend(&sipModuleAPI_{spec.module.py_name}, {get_slot_name(member.py_slot)}, SIP_NULLPTR, sipArg0, sipArg1);
''')
                    else:
                        sf.write(
f'''
    return sipPySlotExtend(&sipModuleAPI_{spec.module.py_name}, {get_slot_name(member.py_slot)}, {backend.get_type_ref(scope)}, sipSelf, sipArg);
''')
                elif is_inplace_number_slot(member.py_slot):
                    sf.write(
'''
    PyErr_Clear();

    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
''')
                else:
                    member_name = '(sipValue != SIP_NULLPTR ? sipName___setattr__ : sipName___delattr__)' if member.py_slot is PySlot.SETATTR else backend.cached_name_ref(member.py_name)

                    sf.write(
f'''
    sipNoMethod(sipParseErr, {backend.cached_name_ref(py_name)}, {member_name}, SIP_NULLPTR);

    return {ret_value};
''')
        else:
            sf.write(
'''
    return 0;
''')

    sf.write('}\n')


def _class_functions(backend, sf, bindings, klass, py_debug):
    """ Generate the member functions for a class. """

    spec = backend.spec
    as_word = klass.iface_file.fq_cpp_name.as_word
    scope_s = backend.scoped_class_name(klass)

    # Any shadow code.
    if klass.has_shadow:
        if not klass.export_derived:
            _shadow_class_declaration(backend, sf, bindings, klass)

        _shadow_code(backend, sf, bindings, klass)

    # The member functions.
    for visible_member in klass.visible_members:
        if visible_member.member.py_slot is None:
            _member_function(backend, sf, bindings, klass,
                    visible_member.member, visible_member.scope)

    # The slot functions.
    for member in klass.members:
        if klass.iface_file.type is IfaceFileType.NAMESPACE:
            _ordinary_function(backend, sf, bindings, member, scope=klass)
        elif member.py_slot is not None:
            _py_slot(backend, sf, bindings, member, scope=klass)

    # The cast function.
    if len(klass.superclasses) != 0:
        sf.write(
f'''

/* Cast a pointer to a type somewhere in its inheritance hierarchy. */
extern "C" {{static void *cast_{as_word}(void *, const sipTypeDef *);}}
static void *cast_{as_word}(void *sipCppV, const sipTypeDef *targetType)
{{
    {_class_from_void(backend, klass)};

    if (targetType == {backend.get_type_ref(klass)})
        return sipCppV;

''')

        for superclass in klass.superclasses:
            sc_fq_cpp_name = superclass.iface_file.fq_cpp_name
            sc_scope_s = backend.scoped_class_name(superclass)
            sc_type_ref = backend.get_type_ref(superclass)

            if len(superclass.superclasses) != 0:
                # Delegate to the super-class's cast function.  This will
                # handle virtual and non-virtual diamonds.
                sf.write(
f'''    sipCppV = ((const sipClassTypeDef *){sc_type_ref})->ctd_cast(static_cast<{sc_scope_s} *>(sipCpp), targetType);
    if (sipCppV)
        return sipCppV;

''')
            else:
                # The super-class is a base class and so doesn't have a cast
                # function.  It also means that a simple check will do instead.
                sf.write(
f'''    if (targetType == {sc_type_ref})
        return static_cast<{sc_scope_s} *>(sipCpp);

''')

        sf.write(
'''    return SIP_NULLPTR;
}
''')

    if klass.iface_file.type is not IfaceFileType.NAMESPACE and not spec.c_bindings:
        # Generate the release function without compiler warnings.
        need_state = False
        need_ptr = need_cast_ptr = is_used_in_code(klass.dealloc_code,
                'sipCpp')

        public_dtor = klass.dtor is AccessSpecifier.PUBLIC

        if klass.can_create or public_dtor:
            if (backend.pyqt5_supported() or backend.pyqt6_supported()) and klass.is_qobject and public_dtor:
                need_ptr = need_cast_ptr = True
            elif klass.has_shadow:
                need_ptr = need_state = True
            elif public_dtor:
                need_ptr = True

        sf.write('\n\n/* Call the instance\'s destructor. */\n')

        sip_cpp_v = 'sipCppV' if spec.c_bindings or need_ptr else ''
        sip_state = ' sipState' if spec.c_bindings or need_state else ''

        if not spec.c_bindings:
            sf.write(f'extern "C" {{static void release_{as_word}(void *, int);}}\n')

        sf.write(f'static void release_{as_word}(void *{sip_cpp_v}, int{sip_state})\n{{\n')

        if need_cast_ptr:
            sf.write(f'    {_class_from_void(backend, klass)};\n\n')

        if len(klass.dealloc_code) != 0:
            sf.write_code(klass.dealloc_code)
            sf.write('\n')

        if klass.can_create or public_dtor:
            rel_gil = release_gil(klass.dtor_gil_action, bindings)

            # If there is an explicit public dtor then assume there is some way
            # to call it which we haven't worked out (because we don't fully
            # understand C++).

            if rel_gil:
                sf.write('    Py_BEGIN_ALLOW_THREADS\n\n')

            if (backend.pyqt5_supported() or backend.pyqt6_supported()) and klass.is_qobject and public_dtor:
                # QObjects should only be deleted in the threads that they
                # belong to.
                sf.write(
'''    if (QThread::currentThread() == sipCpp->thread())
        delete sipCpp;
    else
        sipCpp->deleteLater();
''')
            elif klass.has_shadow:
                sf.write(
f'''    if (sipState & SIP_DERIVED_CLASS)
        delete reinterpret_cast<sip{as_word} *>(sipCppV);
''')

                if public_dtor:
                    sf.write(
f'''    else
        delete reinterpret_cast<{backend.scoped_class_name(klass)} *>(sipCppV);
''')
            elif public_dtor:
                sf.write(
f'''    delete reinterpret_cast<{backend.scoped_class_name(klass)} *>(sipCppV);
''')

            if rel_gil:
                sf.write('\n    Py_END_ALLOW_THREADS\n')

        sf.write('}\n')

    # The traverse function.
    if klass.gc_traverse_code is not None:
        sf.write('\n\n')

        if not spec.c_bindings:
            sf.write(f'extern "C" {{static int traverse_{as_word}(void *, visitproc, void *);}}\n')

        sf.write(
f'''static int traverse_{as_word}(void *sipCppV, visitproc sipVisit, void *sipArg)
{{
    {_class_from_void(backend, klass)};
    int sipRes;

''')

        sf.write_code(klass.gc_traverse_code)

        sf.write(
'''
    return sipRes;
}
''')

    # The clear function.
    if klass.gc_clear_code is not None:
        sf.write('\n\n')

        if not spec.c_bindings:
            sf.write(f'extern "C" {{static int clear_{as_word}(void *);}}\n')

        sf.write(
f'''static int clear_{as_word}(void *sipCppV)
{{
    {_class_from_void(backend, klass)};
    int sipRes;

''')

        sf.write_code(klass.gc_clear_code)

        sf.write(
'''
    return sipRes;
}
''')

    # The buffer interface functions.
    if klass.bi_get_buffer_code is not None:
        code = klass.bi_get_buffer_code

        need_cpp = is_used_in_code(code, 'sipCpp')
        sip_self = _arg_name(spec, 'sipSelf', code)
        sip_cpp_v = 'sipCppV' if spec.c_bindings or need_cpp else ''

        sf.write('\n\n')

        if not py_debug and spec.module.use_limited_api:
            if not spec.c_bindings:
                sf.write(f'extern "C" {{static int getbuffer_{as_word}(PyObject *, void *, sipBufferDef *);}}\n')

            sf.write(f'static int getbuffer_{as_word}(PyObject *{sip_self}, void *{sip_cpp_v}, sipBufferDef *sipBuffer)\n')
        else:
            if not spec.c_bindings:
                sf.write(f'extern "C" {{static int getbuffer_{as_word}(PyObject *, void *, Py_buffer *, int);}}\n')

            sip_flags = _arg_name(spec, 'sipFlags', code)

            sf.write(f'static int getbuffer_{as_word}(PyObject *{sip_self}, void *{sip_cpp_v}, Py_buffer *sipBuffer, int {sip_flags})\n')

        sf.write('{\n')

        if need_cpp:
            sf.write(f'    {_class_from_void(backend, klass)};\n')

        sf.write('    int sipRes;\n\n')
        sf.write_code(code)
        sf.write('\n    return sipRes;\n}\n')

    if klass.bi_release_buffer_code is not None:
        code = klass.bi_release_buffer_code

        need_cpp = is_used_in_code(code, 'sipCpp')
        sip_self = _arg_name(spec, 'sipSelf', code)
        sip_cpp_v = 'sipCppV' if spec.c_bindings or need_cpp else ''

        sf.write('\n\n')

        if not py_debug and spec.module.use_limited_api:
            if not spec.c_bindings:
                sf.write(f'extern "C" {{static void releasebuffer_{as_word}(PyObject *, void *);}}\n')

            sf.write(f'static void releasebuffer_{as_word}(PyObject *{sip_self}, void *{sip_cpp_v})\n')
        else:
            if not spec.c_bindings:
                sf.write(f'extern "C" {{static void releasebuffer_{as_word}(PyObject *, void *, Py_buffer *);}}\n')

            sip_buffer = _arg_name(spec, 'sipBuffer', code)

            sf.write(f'static void releasebuffer_{as_word}(PyObject *{sip_self}, void *{sip_cpp_v}, Py_buffer *{sip_buffer})\n')

        sf.write('{\n')

        if need_cpp:
            sf.write(f'    {_class_from_void(backend, klass)};\n')

        sf.write_code(code)
        sf.write('}\n')

    # The pickle function.
    if klass.pickle_code is not None:
        sf.write('\n\n')

        if not spec.c_bindings:
            sf.write(f'extern "C" {{static PyObject *pickle_{as_word}(void *);}}\n')

        sf.write(
f'''static PyObject *pickle_{as_word}(void *sipCppV)
{{
    {_class_from_void(backend, klass)};
    PyObject *sipRes;

''')

        sf.write_code(klass.pickle_code)

        sf.write('\n    return sipRes;\n}\n')

    # The finalisation function.
    if klass.finalisation_code is not None:
        code = klass.finalisation_code

        need_cpp = is_used_in_code(code, 'sipCpp')
        sip_self = _arg_name(spec, 'sipSelf', code)
        sip_cpp_v = 'sipCppV' if spec.c_bindings or need_cpp else ''
        sip_kwds = _arg_name(spec, 'sipKwds', code)
        sip_unused = _arg_name(spec, 'sipUnused', code)

        sf.write('\n\n')

        if not spec.c_bindings:
            sf.write(f'extern "C" {{static int final_{as_word}(PyObject *, void *, PyObject *, PyObject **);}}\n')

        sf.write(
f'''static int final_{as_word}(PyObject *{sip_self}, void *{sip_cpp_v}, PyObject *{sip_kwds}, PyObject **{sip_unused})
{{
''')

        if need_cpp:
            sf.write(f'    {_class_from_void(backend, klass)};\n\n')

        sf.write_code(code)

        sf.write('}\n')

    # The mixin initialisation function.
    if klass.mixin:
        sf.write('\n\n')

        if not spec.c_bindings:
            sf.write(f'extern "C" {{static int mixin_{as_word}(PyObject *, PyObject *, PyObject *);}}\n')

        sf.write(
f'''static int mixin_{as_word}(PyObject *sipSelf, PyObject *sipArgs, PyObject *sipKwds)
{{
    return sipInitMixin(sipSelf, sipArgs, sipKwds, (sipClassTypeDef *)&sipTypeDef_{spec.module.py_name}_{as_word});
}}
''')

    # The array allocation helpers.
    if spec.c_bindings or klass.needs_array_helper:
        sf.write('\n\n')

        if not spec.c_bindings:
            sf.write(f'extern "C" {{static void *array_{as_word}(Py_ssize_t);}}\n')

        sf.write(f'static void *array_{as_word}(Py_ssize_t sipNrElem)\n{{\n')

        if spec.c_bindings:
            sf.write(f'    return sipMalloc(sizeof ({scope_s}) * sipNrElem);\n')
        else:
            sf.write(f'    return new {scope_s}[sipNrElem];\n')

        sf.write('}\n')

        if backend.abi_supports_array():
            sf.write('\n\n')

            if not spec.c_bindings:
                sf.write(f'extern "C" {{static void array_delete_{as_word}(void *);}}\n')

            sf.write(f'static void array_delete_{as_word}(void *sipCpp)\n{{\n')

            if spec.c_bindings:
                sf.write('    sipFree(sipCpp);\n')
            else:
                sf.write(f'    delete[] reinterpret_cast<{scope_s} *>(sipCpp);\n')

            sf.write('}\n')

    # The copy and assignment helpers.
    if spec.c_bindings or klass.needs_copy_helper:
        # The assignment helper.  We assume that there will be a valid
        # assigment operator if there is a a copy ctor.  Note that the source
        # pointer is not const.  This is to allow the source instance to be
        # modified as a consequence of the assignment, eg. if it is
        # implementing some sort of reference counting scheme.

        sf.write('\n\n')

        if not spec.c_bindings:
            sf.write(f'extern "C" {{static void assign_{as_word}(void *, Py_ssize_t, void *);}}\n')

        sf.write(f'static void assign_{as_word}(void *sipDst, Py_ssize_t sipDstIdx, void *sipSrc)\n{{\n')

        if spec.c_bindings:
            sf.write(f'    (({scope_s} *)sipDst)[sipDstIdx] = *(({scope_s} *)sipSrc);\n')
        else:
            sf.write(f'    reinterpret_cast<{scope_s} *>(sipDst)[sipDstIdx] = *reinterpret_cast<{scope_s} *>(sipSrc);\n')

        sf.write('}\n')

        # The copy helper.
        sf.write('\n\n')

        if not spec.c_bindings:
            sf.write(f'extern "C" {{static void *copy_{as_word}(const void *, Py_ssize_t);}}\n')

        sf.write(f'static void *copy_{as_word}(const void *sipSrc, Py_ssize_t sipSrcIdx)\n{{\n')

        if spec.c_bindings:
            sf.write(
f'''    {scope_s} *sipPtr = sipMalloc(sizeof ({scope_s}));
    *sipPtr = ((const {scope_s} *)sipSrc)[sipSrcIdx];

    return sipPtr;
''')
        else:
            sf.write(
f'''    return new {scope_s}(reinterpret_cast<const {scope_s} *>(sipSrc)[sipSrcIdx]);
''')

        sf.write('}\n')

    # The dealloc function.
    if backend.need_dealloc(bindings, klass):
        sf.write('\n\n')

        if not spec.c_bindings:
            sf.write(f'extern "C" {{static void dealloc_{as_word}(sipSimpleWrapper *);}}\n')

        sf.write(f'static void dealloc_{as_word}(sipSimpleWrapper *sipSelf)\n{{\n')

        backend.g_slot_support_vars(sf)

        if bindings.tracing:
            sf.write(f'    sipTrace(SIP_TRACE_DEALLOCS, "dealloc_{as_word}()\\n");\n\n')

        # Disable the virtual handlers.
        if klass.has_shadow:
            sf.write(
f'''    if (sipIsDerivedClass(sipSelf))
        reinterpret_cast<sip{as_word} *>(sipGetAddress(sipSelf))->sipPySelf = SIP_NULLPTR;

''')

        if spec.c_bindings or klass.dtor is AccessSpecifier.PUBLIC or (klass.has_shadow and klass.dtor is AccessSpecifier.PROTECTED):
            sf.write('    if (sipIsOwnedByPython(sipSelf))\n    {\n')

            if klass.delay_dtor:
                sf.write('        sipAddDelayedDtor(sipSelf);\n')
            elif spec.c_bindings:
                if klass.dealloc_code:
                    sf.write(klass.dealloc_code)

                sf.write('        sipFree(sipGetAddress(sipSelf));\n')
            else:
                flag = 'sipIsDerivedClass(sipSelf)' if klass.has_shadow else '0'
                sf.write(f'        release_{as_word}(sipGetAddress(sipSelf), {flag});\n')

            sf.write('    }\n')

        sf.write('}\n')

    # The type initialisation function.
    if klass.can_create:
        _type_init(backend, sf, bindings, klass)


def _shadow_code(backend, sf, bindings, klass):
    """ Generate the shadow (derived) class code. """

    spec = backend.spec
    klass_name = klass.iface_file.fq_cpp_name.as_word
    klass_cpp_name = klass.iface_file.fq_cpp_name.as_cpp

    # Generate the wrapper class constructors.
    nr_virtuals = _count_virtual_overloads(spec, klass)

    for ctor in _unique_class_ctors(spec, klass):
        throw_specifier = _throw_specifier(bindings, ctor.throw_args)
        protected_call_args = _protected_call_args(spec, ctor.cpp_signature)
        args = fmt_signature_as_cpp_definition(spec, ctor.cpp_signature,
                scope=klass.iface_file)

        sf.write(f'\nsip{klass_name}::sip{klass_name}({args}){throw_specifier}: {backend.scoped_class_name(klass)}({protected_call_args}), sipPySelf(SIP_NULLPTR)\n{{\n')

        if bindings.tracing:
            args = fmt_signature_as_cpp_declaration(spec, ctor.cpp_signature,
                    scope=klass.iface_file)

            sf.write(f'    sipTrace(SIP_TRACE_CTORS, "sip{klass_name}::sip{klass_name}({args}){throw_specifier} (this=0x%08x)\\n", this);\n\n')

        if nr_virtuals > 0:
            sf.write('    memset(sipPyMethods, 0, sizeof (sipPyMethods));\n')

        sf.write('}\n')

    # The destructor.
    if klass.dtor is not AccessSpecifier.PRIVATE:
        throw_specifier = _throw_specifier(bindings, klass.dtor_throw_args)

        sf.write(f'\nsip{klass_name}::~sip{klass_name}(){throw_specifier}\n{{\n')

        if bindings.tracing:
            sf.write(f'    sipTrace(SIP_TRACE_DTORS, "sip{klass_name}::~sip{klass_name}(){throw_specifier} (this=0x%08x)\\n", this);\n\n')

        if klass.dtor_virtual_catcher_code is not None:
            sf.write_code(klass.dtor_virtual_catcher_code)

        sf.write('    sipInstanceDestroyedEx(&sipPySelf);\n}\n')

    # The meta methods if required.
    if (backend.pyqt5_supported() or backend.pyqt6_supported()) and klass.is_qobject:
        module_name = spec.module.py_name
        type_ref = backend.get_type_ref(klass)

        if not klass.pyqt_no_qmetaobject:
            sf.write(
f'''
const QMetaObject *sip{klass_name}::metaObject() const
{{
    if (sipGetInterpreter())
        return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : sip_{module_name}_qt_metaobject(sipPySelf, {type_ref});

    return {klass_cpp_name}::metaObject();
}}
''')

        sf.write(
f'''
int sip{klass_name}::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{{
    _id = {klass_cpp_name}::qt_metacall(_c, _id, _a);

    if (_id >= 0)
    {{
        SIP_BLOCK_THREADS
        _id = sip_{module_name}_qt_metacall(sipPySelf, {type_ref}, _c, _id, _a);
        SIP_UNBLOCK_THREADS
    }}

    return _id;
}}

void *sip{klass_name}::qt_metacast(const char *_clname)
{{
    void *sipCpp;

    return (sip_{module_name}_qt_metacast(sipPySelf, {type_ref}, _clname, &sipCpp) ? sipCpp : {klass_cpp_name}::qt_metacast(_clname));
}}
''')

    # Generate the virtual catchers.
    for virt_nr, virtual_overload in enumerate(_unique_class_virtual_overloads(spec, klass)):
        _virtual_catcher(backend, sf, bindings, klass, virtual_overload,
                virt_nr)

    # Generate the wrapper around each protected member function.
    _protected_definitions(sf, spec, klass)


def _protected_enums(sf, spec, klass):
    """ Generate the protected enums for a class. """

    for enum in spec.enums:
        if not enum.is_protected:
            continue

        # See if the class defining the enum is in our class hierachy.
        for mro_klass in klass.mro:
            if mro_klass is enum.scope:
                break
        else:
            continue

        sf.write(
'''
    /* Expose this protected enum. */
    enum''')

        if enum.fq_cpp_name is not None:
            sf.write(' sip' + enum.fq_cpp_name.base_name)

        sf.write(' {')

        eol = '\n'
        scope_cpp_name = enum.scope.iface_file.fq_cpp_name.as_cpp

        for member in enum.members:
            member_cpp_name = member.cpp_name

            sf.write(f'{eol}        {member_cpp_name} = {scope_cpp_name}::{member_cpp_name}')

            eol = ',\n'

        sf.write('\n    };\n')


def _virtual_catcher(backend, sf, bindings, klass, virtual_overload, virt_nr):
    """ Generate the catcher for a virtual function. """

    spec = backend.spec
    overload = virtual_overload.overload
    result = overload.cpp_signature.result

    result_type = fmt_argument_as_cpp_type(spec, result,
            scope=klass.iface_file, make_public=True)
    klass_name = klass.iface_file.fq_cpp_name.as_word
    overload_cpp_name = _overload_cpp_name(overload)
    throw_specifier = _throw_specifier(bindings, overload.throw_args)
    const = ' const' if overload.is_const else ''

    protection_state = set()
    _remove_protections(overload.cpp_signature, protection_state)

    args = fmt_signature_as_cpp_definition(spec, overload.cpp_signature,
            scope=klass.iface_file)
    sf.write(f'\n{result_type} sip{klass_name}::{overload_cpp_name}({args}){const}{throw_specifier}\n{{\n')

    if bindings.tracing:
        args = fmt_signature_as_cpp_declaration(spec, overload.cpp_signature,
                scope=klass.iface_file)
        sf.write(f'    sipTrace(SIP_TRACE_CATCHERS, "{result_type} sip{klass_name}::{overload_cpp_name}({args}){const}{throw_specifier} (this=0x%08x)\\n", this);\n\n')

    _restore_protections(protection_state)

    sf.write(
'''    sip_gilstate_t sipGILState;
    PyObject *sipMeth;

''')

    if overload.is_const:
        const_cast_char = 'const_cast<char *>('
        const_cast_sw = 'const_cast<sipSimpleWrapper **>('
        const_cast_tail = ')'
    else:
        const_cast_char = ''
        const_cast_sw = ''
        const_cast_tail = ''

    abi_12_8_arg = f'{const_cast_sw}&sipPySelf{const_cast_tail}, ' if spec.target_abi >= (12, 8) else ''

    klass_py_name_ref = backend.cached_name_ref(klass.py_name) if overload.is_abstract else 'SIP_NULLPTR'
    member_py_name_ref = backend.cached_name_ref(overload.common.py_name)

    sf.write(f'    sipMeth = sipIsPyMethod(&sipGILState, {const_cast_char}&sipPyMethods[{virt_nr}]{const_cast_tail}, {abi_12_8_arg}{klass_py_name_ref}, {member_py_name_ref});\n')

    # The rest of the common code.

    if result is not None and result.type is ArgumentType.VOID and len(result.derefs) == 0:
        result = None

    sf.write('\n    if (!sipMeth)\n')

    if overload.virtual_call_code is not None:
        sf.write('    {\n')

        if result is not None:
            sip_res = fmt_argument_as_cpp_type(spec, result, name='sipRes',
                    scope=klass.iface_file)
            sf.write(f'        {sip_res};\n')

        sf.write('\n')

        sf.write_code(overload.virtual_call_code)

        sip_res = ' sipRes' if result is not None else ''
        sf.write(
f'''
        return{sip_res};
    }}
''')

    elif overload.is_abstract:
        _default_instance_return(backend, sf, result)

    else:
        if result is None:
            sf.write('    {\n        ')
        else:
            sf.write('        return ')

        args = []
        for arg_nr, arg in enumerate(overload.cpp_signature.args):
            args.append(fmt_argument_as_name(spec, arg, arg_nr))
        args = ', '.join(args)
 
        sf.write(f'{klass.iface_file.fq_cpp_name.as_cpp}::{overload_cpp_name}({args});\n')
 
        if result is None:
            # Note that we should also generate this if the function returns a
            # value, but we are lazy and this is all that is needed by PyQt.
            if overload.new_thread:
                sf.write('        sipEndThread();\n')

            sf.write('        return;\n    }\n')

    sf.write('\n')

    _virtual_handler_call(sf, spec, klass, virtual_overload, result)

    sf.write('}\n')


def _virtual_handler_call(sf, spec, klass, virtual_overload, result):
    """ Generate a call to a single virtual handler. """

    module = spec.module
    overload = virtual_overload.overload
    handler = virtual_overload.handler

    module_name = module.py_name

    protection_state = _fake_protected_args(handler.cpp_signature)

    result_type = fmt_argument_as_cpp_type(spec, overload.cpp_signature.result,
            scope=klass.iface_file)

    sf.write(f'    extern {result_type} sipVH_{module_name}_{handler.handler_nr}(sip_gilstate_t, sipVirtErrorHandlerFunc, sipSimpleWrapper *, PyObject *')

    if len(handler.cpp_signature.args) > 0:
        sf.write(', ' + fmt_signature_as_cpp_declaration(spec,
                handler.cpp_signature, scope=klass.iface_file))

    _restore_protected_args(protection_state)

    # Add extra arguments for all the references we need to keep.
    args_keep = False
    result_keep = False
    saved_keys = {}

    if result is not None and backend.keep_py_reference(result):
        result_keep = True
        saved_keys[result] = result.key
        result.key = module.next_key
        module.next_key -= 1
        sf.write(', int')

    for arg in overload.cpp_signature.args:
        if arg.is_out and backend.keep_py_reference(arg):
            args_keep = True
            saved_keys[arg] = arg.key
            arg.key = module.next_key
            module.next_key -= 1
            sf.write(', int')

    sf.write(');\n\n    ')

    trailing = ''

    if not overload.new_thread and result is not None:
        sf.write('return ')

        if result.type is ArgumentType.ENUM and result.definition.is_protected:
            protection_state = set()
            _remove_protection(result, protection_state)

            enum_type = fmt_enum_as_cpp_type(result.definition)
            sf.write(f'static_cast<{enum_type}>(')
            trailing = ')'

            _restore_protections(protection_state)

    error_handler = handler.virtual_error_handler

    if error_handler is None:
        error_handler_ref = '0'
    elif error_handler.module is module:
        error_handler_ref = f'sipVEH_{module_name}_{error_handler.name}'
    else:
        # TODO ABI v14 will get the handler directly from the imported module's
        # definition (possibly via an API call) rather than taking a copy of
        # the handler.
        error_handler_ref = f'sipImportedVirtErrorHandlers_{module_name}_{error_handler.module.py_name}[{error_handler.handler_nr}].iveh_handler'

    sf.write(f'sipVH_{module_name}_{handler.handler_nr}(sipGILState, {error_handler_ref}, sipPySelf, sipMeth')

    for arg_nr, arg in enumerate(overload.cpp_signature.args):
        prefix = ''

        if arg.type is ArgumentType.CLASS and arg.definition.is_protected:
            if arg.is_reference or len(arg.derefs) == 0:
                prefix = '&'
        elif arg.type is ArgumentType.ENUM and arg.definition.is_protected:
            prefix = '(' + fmt_enum_as_cpp_type(arg.definition) + ')'

        arg_name = fmt_argument_as_name(spec, arg, arg_nr)

        sf.write(f', {prefix}{arg_name}')

    # Pass the keys to maintain the kept references.
    if result_keep:
        sf.write(', ' + str(result.key))

    if args_keep:
        for arg in overload.cpp_signature.args:
            if arg.is_out and backend.keep_py_reference(arg):
                sf.write(', ' + str(arg.key))

    for type, key in saved_keys.items():
        type.key = key

    sf.write(f'){trailing};\n')

    if overload.new_thread:
        sf.write('\n    sipEndThread();\n')


def _cast_zero(backend, arg):
    """ Return a cast to zero. """

    if arg.type is ArgumentType.ENUM:
        enum = arg.definition
        enum_type = fmt_enum_as_cpp_type(enum)

        if len(enum.members) == 0:
            return f'({enum_type})0'

        if enum.is_scoped:
            scope = enum_type
        elif enum.scope is not None:
            scope = backend.get_enum_class_scope(enum)
        else:
            scope = ''

        return scope + '::' + enum.members[0].cpp_name

    if arg.type in (ArgumentType.PYOBJECT, ArgumentType.PYTUPLE, ArgumentType.PYLIST, ArgumentType.PYDICT, ArgumentType.PYCALLABLE, ArgumentType.PYSLICE, ArgumentType.PYTYPE, ArgumentType.PYBUFFER, ArgumentType.PYENUM, ArgumentType.ELLIPSIS):
        return 'SIP_NULLPTR'

    return '0'


def _default_instance_return(backend, sf, result):
    """ Generate a statement to return the default instance of a type typically
    on error (ie. when there is nothing sensible to return).
    """

    # Handle the trivial case.
    if result is None:
        sf.write('        return;\n')
        return

    spec = backend.spec

    result_type_plain = fmt_argument_as_cpp_type(spec, result, plain=True,
            no_derefs=True)

    # Handle any %InstanceCode.
    if len(result.derefs) == 0 and result.type in (ArgumentType.CLASS, ArgumentType.MAPPED):
        instance_code = result.definition.instance_code
    else:
        instance_code = None

    if instance_code is not None:
        sf.write(
f'''    {{
        static {result_type_plain} *sipCpp = SIP_NULLPTR;

        if (!sipCpp)
        {{
''')

        sf.write_code(instance_code)

        sf.write(
'''        }

        return *sipCpp;
    }
''')

        return

    sf.write('        return ')

    if result.type is ArgumentType.MAPPED and len(result.derefs) == 0:
        # We don't know anything about the mapped type so we just hope is has a
        # default ctor.

        if result.is_reference:
            sf.write('*new ')

        sf.write(result_type_plain + '()')

    elif result.type is ArgumentType.CLASS and len(result.derefs) == 0:
        # If we don't have a suitable ctor then the generated code will issue
        # an error message.

        ctor = result.definition.default_ctor

        if ctor is not None and ctor.access_specifier is AccessSpecifier.PUBLIC and ctor.cpp_signature is not None:
            # If this is a badly designed class.  We can only generate correct
            # code by leaking memory.
            if result.is_reference:
                sf.write('*new ')

            sf.write(result_type_plain)
            sf.write(_call_default_ctor(spec, ctor))
        else:
            raise UserException(
                    result.definition.iface_file.fq_cpp_name.as_cpp + " must have a default constructor")

    else:
        sf.write(_cast_zero(backend, result))

    sf.write(';\n')


def _call_default_ctor(spec, ctor):
    """ Return the call to a default ctor. """

    args = []

    for arg in ctor.cpp_signature.args:
        if arg.default_value is not None:
            break

        # Do what we can to provide type information to the compiler.
        if arg.type is ArgumentType.CLASS and len(arg.derefs) > 0 and not arg.is_reference:
            class_type = fmt_argument_as_cpp_type(spec, arg)
            arg_s = f'static_cast<{class_type}>(0)'
        elif arg.type is ArgumentType.ENUM:
            enum_type = fmt_enum_as_cpp_type(arg.definition)
            arg_s = f'static_cast<{enum_type}>(0)'
        elif arg.type in (ArgumentType.FLOAT, ArgumentType.CFLOAT):
            arg_s = '0.0F'
        elif arg.type in (ArgumentType.DOUBLE, ArgumentType.CDOUBLE):
            arg_s = '0.0'
        elif arg.type in (ArgumentType.UINT, ArgumentType.SIZE):
            arg_s = '0U'
        elif arg.type in (ArgumentType.LONG, ArgumentType.LONGLONG):
            arg_s = '0L'
        elif arg.type in (ArgumentType.ULONG, ArgumentType.ULONGLONG):
            arg_s = '0UL'
        elif arg.type in (ArgumentType.ASCII_STRING, ArgumentType.LATIN1_STRING, ArgumentType.UTF8_STRING, ArgumentType.USTRING, ArgumentType.SSTRING, ArgumentType.STRING) and len(arg.derefs) == 0:
            arg_s = "'\\0'"
        elif arg.type is ArgumentType.WSTRING and len(arg.derefs) == 0:
            arg_s = "L'\\0'"
        else:
            arg_s = '0'

        args.append(arg_s)

    return '(' + ', '.join(args) + ')'


def _protected_declarations(sf, spec, klass):
    """ Generate the declarations of the protected wrapper functions for a
    class.
    """

    no_intro = True

    for visible_member in klass.visible_members:
        if visible_member.member.py_slot is not None:
            continue

        for overload in visible_member.scope.overloads:
            if overload.common is not visible_member.member or overload.access_specifier is not AccessSpecifier.PROTECTED:
                continue

            # Check we haven't already handled this signature (eg. if we have
            # specified the same method with different Python names.
            if _is_duplicate_protected(spec, klass, overload):
                continue

            if no_intro:
                sf.write(
'''
    /*
     * There is a public method for every protected method visible from
     * this class.
     */
''')

                no_intro = False

            sf.write('    ')

            if overload.is_static:
                sf.write('static ')

            result_type = fmt_argument_as_cpp_type(spec,
                    overload.cpp_signature.result, scope=klass.iface_file)

            if not overload.is_static and not overload.is_abstract and (overload.is_virtual or overload.is_virtual_reimplementation):
                sf.write(f'{result_type} sipProtectVirt_{overload.cpp_name}(bool')

                if len(overload.cpp_signature.args) > 0:
                    sf.write(', ')
            else:
                sf.write(f'{result_type} sipProtect_{overload.cpp_name}(')

            args = fmt_signature_as_cpp_declaration(spec,
                    overload.cpp_signature, scope=klass.iface_file)
            const_s = ' const' if overload.is_const else ''

            sf.write(f'{args}){const_s};\n')


def _protected_definitions(sf, spec, klass):
    """ Generate the definitions of the protected wrapper functions for a
    class.
    """

    klass_name = klass.iface_file.fq_cpp_name.as_word

    for visible_member in klass.visible_members:
        if visible_member.member.py_slot is not None:
            continue

        for overload in visible_member.scope.overloads:
            if overload.common is not visible_member.member or overload.access_specifier is not AccessSpecifier.PROTECTED:
                continue

            # Check we haven't already handled this signature (eg. if we have
            # specified the same method with different Python names.
            if _is_duplicate_protected(spec, klass, overload):
                continue

            overload_name = overload.cpp_name
            result = overload.cpp_signature.result
            result_type = fmt_argument_as_cpp_type(spec, result,
                    scope=klass.iface_file)

            sf.write('\n')

            if not overload.is_static and not overload.is_abstract and (overload.is_virtual or overload.is_virtual_reimplementation):
                sf.write(f'{result_type} sip{klass_name}::sipProtectVirt_{overload_name}(bool sipSelfWasArg')

                if len(overload.cpp_signature.args) > 0:
                    sf.write(', ')
            else:
                sf.write(f'{result_type} sip{klass_name}::sipProtect_{overload_name}(')

            args = fmt_signature_as_cpp_definition(spec,
                    overload.cpp_signature, scope=klass.iface_file)
            const_s = ' const' if overload.is_const else ''

            sf.write(f'{args}){const_s}\n{{\n')

            closing_parens = ')'

            if result.type is ArgumentType.VOID and len(result.derefs) == 0:
                sf.write('    ')
            else:
                sf.write('    return ')

                if result.type is ArgumentType.CLASS and result.definition.is_protected:
                    scope_s = backend.scoped_class_name(klass)
                    sf.write(f'static_cast<{scope_s} *>(')
                    closing_parens += ')'
                elif result.type is ArgumentType.ENUM and result.definition.is_protected:
                    # One or two older compilers can't handle a static_cast
                    # here so we revert to a C-style cast.
                    sf.write('(' + fmt_enum_as_cpp_type(result.definition) + ')')

            protected_call_args = _protected_call_args(spec,
                    overload.cpp_signature)

            if not overload.is_abstract:
                visible_scope_s = backend.scoped_class_name(
                        visible_member.scope)

                if overload.is_virtual or overload.is_virtual_reimplementation:
                    sf.write(f'(sipSelfWasArg ? {visible_scope_s}::{overload_name}({protected_call_args}) : ')
                    closing_parens += ')'
                else:
                    sf.write(visible_scope_s + '::')

            sf.write(f'{overload_name}({protected_call_args}{closing_parens};\n}}\n')


def _is_duplicate_protected(spec, klass, target_overload):
    """ Return True if a protected method is a duplicate. """

    for visible_member in klass.visible_members:
        if visible_member.member.py_slot is not None:
            continue

        for overload in visible_member.scope.overloads:
            if overload.common is not visible_member.member or overload.access_specifier is not AccessSpecifier.PROTECTED:
                continue

            if overload is target_overload:
                return False

            if overload.cpp_name == target_overload.cpp_name and same_signature(spec, overload.cpp_signature, target_overload.cpp_signature):
                return True

    # We should never get here.
    return False


def _protected_call_args(spec, signature):
    """ Return the arguments for a call to a protected method. """

    args = []

    for arg_nr, arg in enumerate(signature.args):
        if arg.type is ArgumentType.ENUM and arg.definition.is_protected:
            cast_s = '(' + arg.definition.fq_cpp_name.as_cpp + ')'
        else:
            cast_s = ''

        args.append(cast_s + fmt_argument_as_name(spec, arg, arg_nr))

    return ', '.join(args)


def _virtual_handler(backend, sf, handler):
    """ Generate the function that does most of the work to handle a particular
    virtual function.
    """

    spec = backend.spec
    module = spec.module
    result = handler.cpp_signature.result

    result_decl = fmt_argument_as_cpp_type(spec, result)

    result_is_returned = (result.type is not ArgumentType.VOID or len(result.derefs) != 0)
    result_is_reference = False
    result_instance_code = None

    if result_is_returned:
        # If we are returning a reference to an instance then we take care to
        # handle Python errors but still return a valid C++ instance.
        if result.type in (ArgumentType.CLASS, ArgumentType.MAPPED) and len(result.derefs) == 0:
            if result.is_reference:
                result_is_reference = True
            else:
                result_instance_code = result.definition.instance_code

    sf.write(
f'''
{result_decl} sipVH_{module.py_name}_{handler.handler_nr}(sip_gilstate_t sipGILState, sipVirtErrorHandlerFunc sipErrorHandler, sipSimpleWrapper *sipPySelf, PyObject *sipMethod''')

    if len(handler.cpp_signature.args) > 0:
        sf.write(', ' + fmt_signature_as_cpp_definition(spec,
                handler.cpp_signature))

    # Define the extra arguments for kept references.
    if result_is_returned and backend.keep_py_reference(result):
        sf.write(', int')

        if handler.virtual_catcher_code is None or is_used_in_code(handler.virtual_catcher_code, 'sipResKey'):
            sf.write(' sipResKey')

    for arg_nr, arg in enumerate(handler.cpp_signature.args):
        if arg.is_out and backend.keep_py_reference(arg):
            arg_name = fmt_argument_as_name(spec, arg, arg_nr)
            sf.write(f', int {arg_name}Key')

    sf.write(')\n{\n')

    if result_is_returned:
        result_plain_decl = fmt_argument_as_cpp_type(spec, result, plain=True)

        if result_instance_code is not None:
            sf.write(
f'''    static {result_plain_decl} *sipCpp = SIP_NULLPTR;

    if (!sipCpp)
    {{
''')

            sf.write_code(result_instance_code)

            sf.write('    }\n\n')

        sf.write('    ')

        # wchar_t * return values are always on the heap.  To reduce memory
        # leaks we keep the last result around until we have a new one.  This
        # means that ownership of the return value stays with the function
        # returning it - which is consistent with how other types work, even
        # though it may not be what's required in all cases.  Note that we
        # should do this in the code that calls the handler instead of here (as
        # we do with strings) so that it doesn't get shared between all
        # callers.
        if result.type is ArgumentType.WSTRING and len(result.derefs) == 1:
            sf.write('static ')

        sf.write(result_plain_decl)

        sf.write(' {}sipRes'.format('*' if result_is_reference else ''))

        sipres_value = ''

        if result.type in (ArgumentType.CLASS, ArgumentType.MAPPED, ArgumentType.TEMPLATE) and len(result.derefs) == 0:
            if result_instance_code is not None:
                sipres_value = ' = *sipCpp'
            elif result.type is ArgumentType.CLASS:
                ctor = result.definition.default_ctor

                if ctor is not None and ctor.access_specifier is AccessSpecifier.PUBLIC and ctor.cpp_signature is not None and len(ctor.cpp_signature.args) > 0 and ctor.cpp_signature.args[0].default_value is None:
                    sipres_value = _call_default_ctor(spec, ctor)
        elif result.type is ArgumentType.ENUM and result.definition.is_protected:
            # Currently SIP generates the virtual handlers before any shadow
            # classes which means that the compiler doesn't know about the
            # handling of protected enums.  Therefore we can only initialise to
            # 0.
            sipres_value = ' = 0'
        else:
            # We initialise the result to try and suppress a compiler warning.
            sipres_value = ' = ' + _cast_zero(backend, result)

        sf.write(sipres_value + ';\n')

        if result.type is ArgumentType.WSTRING and len(result.derefs) == 1:
            free_arg = 'const_cast<wchar_t *>(sipRes)' if result.is_const else 'sipRes'

            sf.write(
f'''
    if (sipRes)
    {{
        // Return any previous result to the heap.
        sipFree({free_arg});
        sipRes = SIP_NULLPTR;
    }}

''')

    if handler.virtual_catcher_code is not None:
        error_flag = need_error_flag(handler.virtual_catcher_code)
        old_error_flag = backend.need_deprecated_error_flag(
                handler.virtual_catcher_code)

        if error_flag:
            sf.write('    sipErrorState sipError = sipErrorNone;\n')
        elif old_error_flag:
            sf.write('    int sipIsErr = 0;\n')

        sf.write('\n')

        sf.write_code(handler.virtual_catcher_code)

        sf.write(
'''
    Py_DECREF(sipMethod);
''')

        if error_flag or old_error_flag:
            error_test = 'sipError != sipErrorNone' if error_flag else 'sipIsErr'

            sf.write(
f'''
    if ({error_test})
        sipCallErrorHandler(sipErrorHandler, sipPySelf, sipGILState);
''')

        sf.write(
'''
    SIP_RELEASE_GIL(sipGILState)
''')

        if result_is_returned:
            sf.write(
'''
    return sipRes;
''')

        sf.write('}\n')

        return

    # See how many values we expect.
    nr_values = 1 if result_is_returned else 0

    for arg in handler.py_signature.args:
        if arg.is_out:
            nr_values += 1

    # Call the method.
    if nr_values == 0:
        sf.write(
'    sipCallProcedureMethod(sipGILState, sipErrorHandler, sipPySelf, sipMethod, ')
    else:
        sf.write(
'    PyObject *sipResObj = sipCallMethod(SIP_NULLPTR, sipMethod, ')

    sf.write(_tuple_builder(backend, handler.py_signature))

    if nr_values == 0:
        sf.write(''');
}
''')

        return

    # Generate the call to sipParseResultEx().
    params = ['sipGILState', 'sipErrorHandler', 'sipPySelf', 'sipMethod',
            'sipResObj']

    # Build the format string.
    fmt = '"'

    if nr_values == 0:
        fmt += 'Z'
    else:
        if nr_values > 1:
            fmt += '('

        if result_is_returned:
            fmt += _get_parse_result_format(result, spec,
                    result_is_reference=result_is_reference,
                    transfer_result=handler.transfer_result)

        for arg in handler.py_signature.args:
            if arg.is_out:
                fmt += _get_parse_result_format(arg, spec)

        if nr_values > 1:
            fmt += ')'

    fmt += '"'

    params.append(fmt)

    # Add the destination pointers.
    if result_is_returned:
        _add_parse_result_extra_params(backend, params, module, result)
        params.append('&sipRes')

    for arg_nr, arg in enumerate(handler.py_signature.args):
        if arg.is_out:
            _add_parse_result_extra_params(backend, params, module, arg,
                    arg_nr)

            arg_ref = '&' if arg.is_reference else ''
            arg_name = fmt_argument_as_name(spec, arg, arg_nr)
            params.append(arg_ref + arg_name)

    params = ', '.join(params)

    return_code = 'int sipRc = ' if result_is_reference or handler.abort_on_exception else ''

    sf.write(f''');

    {return_code}sipParseResultEx({params});
''')

    if result_is_returned:
        if result_is_reference or handler.abort_on_exception:
            sf.write(
'''
    if (sipRc < 0)
''')

            if handler.abort_on_exception:
                sf.write('        abort();\n')
            else:
                _default_instance_return(backend, sf, result)

        result_ref = '*' if result_is_reference else ''

        sf.write(
f'''
    return {result_ref}sipRes;
''')

    sf.write('}\n')


def _add_parse_result_extra_params(backend, params, module, arg, arg_nr=-1):
    """ Add any extra parameters needed by sipParseResultEx() for a particular
    type to a list.
    """

    if arg.type in (ArgumentType.CLASS, ArgumentType.MAPPED, ArgumentType.ENUM):
        params.append(backend.get_type_ref(arg.definition))
    elif arg.type is ArgumentType.PYTUPLE:
        params.append('&PyTuple_Type')
    elif arg.type is ArgumentType.PYLIST:
        params.append('&PyList_Type')
    elif arg.type is ArgumentType.PYDICT:
        params.append('&PyDict_Type')
    elif arg.type is ArgumentType.PYSLICE:
        params.append('&PySlice_Type')
    elif arg.type is ArgumentType.PYTYPE:
        params.append('&PyType_Type')
    elif arg.type is ArgumentType.CAPSULE:
        params.append('"' + arg.definition.as_cpp + '"')
    elif backend.keep_py_reference(arg):
        if arg_nr < 0:
            params.append('sipResKey')
        else:
            params.append(
                    fmt_argument_as_name(backend.spec, arg, arg_nr) + 'Key')


def _get_parse_result_format(arg, spec, result_is_reference=False,
        transfer_result=False):
    """ Return the format characters used by sipParseResultEx() for a
    particular type.
    """

    nr_derefs = len(arg.derefs)
    no_derefs = (nr_derefs == 0)

    if arg.type in (ArgumentType.MAPPED, ArgumentType.FAKE_VOID, ArgumentType.CLASS):
        f = 0x00

        if nr_derefs == 0:
            f |= 0x01

            if not result_is_reference:
                f |= 0x04
        elif nr_derefs == 1:
            if arg.is_out:
                f |= 0x04
            elif arg.disallow_none:
                f |= 0x01

        if transfer_result:
            f |= 0x02

        return 'H' + str(f)

    if arg.type in (ArgumentType.BOOL, ArgumentType.CBOOL):
        return 'b'

    if arg.type is ArgumentType.ASCII_STRING:
        return 'aA' if no_derefs else 'AA'

    if arg.type is ArgumentType.LATIN1_STRING:
        return 'aL' if no_derefs else 'AL'

    if arg.type is ArgumentType.UTF8_STRING:
        return 'a8' if no_derefs else 'A8'

    if arg.type in (ArgumentType.SSTRING, ArgumentType.USTRING, ArgumentType.STRING):
        return 'c' if no_derefs else 'B'

    if arg.type is ArgumentType.WSTRING:
        return 'w' if no_derefs else 'x'

    if arg.type is ArgumentType.ENUM:
        return 'F' if arg.definition.fq_cpp_name is not None else 'e'

    if arg.type is ArgumentType.BYTE:
        return 'I' if backend.abi_has_working_char_conversion() else 'L'

    if arg.type is ArgumentType.SBYTE:
        return 'L'

    if arg.type is ArgumentType.UBYTE:
        return 'M'

    if arg.type is ArgumentType.USHORT:
        return 't'

    if arg.type is ArgumentType.SHORT:
        return 'h'

    if arg.type in (ArgumentType.INT, ArgumentType.CINT):
        return 'i'

    if arg.type is ArgumentType.UINT:
        return 'u'

    if arg.type is ArgumentType.SIZE:
        return '='

    if arg.type is ArgumentType.LONG:
        return 'l'

    if arg.type is ArgumentType.ULONG:
        return 'm'

    if arg.type is ArgumentType.LONGLONG:
        return 'n'

    if arg.type is ArgumentType.ULONGLONG:
        return 'o'

    if arg.type in (ArgumentType.STRUCT, ArgumentType.UNION, ArgumentType.VOID):
        return 'V'

    if arg.type is ArgumentType.CAPSULE:
        return 'z'

    if arg.type in (ArgumentType.FLOAT, ArgumentType.CFLOAT):
        return 'f'

    if arg.type in (ArgumentType.DOUBLE, ArgumentType.CDOUBLE):
        return 'd'

    if arg.type is ArgumentType.PYOBJECT:
        return 'O'

    if arg.type in (ArgumentType.PYTUPLE, ArgumentType.PYLIST, ArgumentType.PYDICT, ArgumentType.SLICE, ArgumentType.PYTYPE):
        return 'N' if arg.allow_none else 'T'

    if arg.type is ArgumentType.PYBUFFER:
        return '$' if arg.allow_none else '!'

    if arg.type is ArgumentType.PYENUM:
        return '^' if arg.allow_none else '&'

    # We should never get here.
    return ' '


def _tuple_builder(backend, signature):
    """ Return the code to build a tuple of Python arguments. """

    spec = backend.spec
    array_len_arg_nr = -1
    format_s = '"'

    for arg_nr, arg in enumerate(signature.args):
        if not arg.is_in:
            continue

        format_ch = ''
        nr_derefs = len(arg.derefs)
        not_a_pointer = (nr_derefs == 0 or (nr_derefs == 1 and arg.is_out))

        if arg.type in (ArgumentType.ASCII_STRING, ArgumentType.LATIN1_STRING, ArgumentType.UTF8_STRING):
            format_ch = 'a' if not_a_pointer else 'A'

        elif arg.type in (ArgumentType.SSTRING, ArgumentType.USTRING, ArgumentType.STRING):
            if not_a_pointer:
                format_ch = 'c'
            elif arg.array is ArrayArgument.ARRAY:
                format_ch = 'g'
            else:
                format_ch = 's'

        elif arg.type is ArgumentType.WSTRING:
            if not_a_pointer:
                format_ch = 'w'
            elif arg.array is ArrayArgument.ARRAY:
                format_ch = 'G'
            else:
                format_ch = 'x'

        elif arg.type in (ArgumentType.BOOL, ArgumentType.CBOOL):
            format_ch = 'b'

        elif arg.type is ArgumentType.ENUM:
            format_ch = 'e' if arg.definition.fq_cpp_name is None else 'F'

        elif arg.type is ArgumentType.CINT:
            format_ch = 'i'

        elif arg.type is ArgumentType.UINT:
            if arg.array is ArrayArgument.ARRAY_SIZE:
                array_len_arg_nr = arg_nr
            else:
                format_ch = 'u'

        elif arg.type is ArgumentType.INT:
            if arg.array is ArrayArgument.ARRAY_SIZE:
                array_len_arg_nr = arg_nr
            else:
                format_ch = 'i'

        elif arg.type is ArgumentType.SIZE:
            if arg.array is ArrayArgument.ARRAY_SIZE:
                array_len_arg_nr = arg_nr
            else:
                format_ch = '='

        elif arg.type in (ArgumentType.BYTE, ArgumentType.SBYTE):
            if arg.array is ArrayArgument.ARRAY_SIZE:
                array_len_arg_nr = arg_nr
            else:
                # Note that this is the correct thing to do even if char is
                # unsigned.
                format_ch = 'L'

        elif arg.type is ArgumentType.UBYTE:
            if arg.array is ArrayArgument.ARRAY_SIZE:
                array_len_arg_nr = arg_nr
            else:
                format_ch = 'M'

        elif arg.type is ArgumentType.USHORT:
            if arg.array is ArrayArgument.ARRAY_SIZE:
                array_len_arg_nr = arg_nr
            else:
                format_ch = 't'

        elif arg.type is ArgumentType.SHORT:
            if arg.array is ArrayArgument.ARRAY_SIZE:
                array_len_arg_nr = arg_nr
            else:
                format_ch = 'h'

        elif arg.type is ArgumentType.LONG:
            if arg.array is ArrayArgument.ARRAY_SIZE:
                array_len_arg_nr = arg_nr
            else:
                format_ch = 'l'

        elif arg.type is ArgumentType.ULONG:
            if arg.array is ArrayArgument.ARRAY_SIZE:
                array_len_arg_nr = arg_nr
            else:
                format_ch = 'm'

        elif arg.type is ArgumentType.LONGLONG:
            if arg.array is ArrayArgument.ARRAY_SIZE:
                array_len_arg_nr = arg_nr
            else:
                format_ch = 'n'

        elif arg.type is ArgumentType.ULONGLONG:
            if arg.array is ArrayArgument.ARRAY_SIZE:
                array_len_arg_nr = arg_nr
            else:
                format_ch = 'o'

        elif arg.type in (ArgumentType.STRUCT, ArgumentType.UNION, ArgumentType.VOID):
            format_ch = 'V'

        elif arg.type is ArgumentType.CAPSULE:
            format_ch = 'z'

        elif arg.type in (ArgumentType.FLOAT, ArgumentType.CFLOAT):
            format_ch = 'f'

        elif arg.type in (ArgumentType.DOUBLE, ArgumentType.CDOUBLE):
            format_ch = 'd'

        elif arg.type in (ArgumentType.MAPPED, ArgumentType.CLASS):
            if arg.array is ArrayArgument.ARRAY:
                format_ch = 'r'
            else:
                format_ch = 'N' if _needs_heap_copy(arg) else 'D'

        elif arg.type is ArgumentType.FAKE_VOID:
            format_ch = 'D'

        elif arg.type in (ArgumentType.PYOBJECT, ArgumentType.PYTUPLE, ArgumentType.PYLIST, ArgumentType.PYDICT, ArgumentType.PYCALLABLE, ArgumentType.PYSLICE, ArgumentType.PYTYPE, ArgumentType.PYBUFFER, ArgumentType.PYENUM):
            format_ch = 'S'

        format_s += format_ch

    format_s += '"'

    args = [format_s]

    for arg_nr, arg in enumerate(signature.args):
        if not arg.is_in:
            continue

        nr_derefs = len(arg.derefs)

        if arg.type in (ArgumentType.ASCII_STRING, ArgumentType.LATIN1_STRING, ArgumentType.UTF8_STRING, ArgumentType.SSTRING, ArgumentType.USTRING, ArgumentType.STRING, ArgumentType.WSTRING):
            if not (nr_derefs == 0 or (nr_derefs == 1 and arg.is_out)):
                nr_derefs -= 1

        elif arg.type in (ArgumentType.MAPPED, ArgumentType.CLASS, ArgumentType.FAKE_VOID):
            if nr_derefs > 0:
                nr_derefs -= 1

        elif arg.type in (ArgumentType.STRUCT, ArgumentType.UNION, ArgumentType.VOID):
            nr_derefs -= 1

        if arg.type in (ArgumentType.MAPPED, ArgumentType.CLASS, ArgumentType.FAKE_VOID):
            prefix = ''
            ref = ''

            needs_copy = _needs_heap_copy(arg)

            if needs_copy:
                prefix = 'new ' + fmt_argument_as_cpp_type(spec, arg, plain=True, no_derefs=True) + '('
            else:
                if arg.is_const:
                    prefix = 'const_cast<' + fmt_argument_as_cpp_type(spec, arg, plain=True, no_derefs=True, use_typename=False) + ' *>('

                if len(arg.derefs) == 0:
                    ref = '&'
                else:
                    ref = '*' * nr_derefs

            suffix = '' if prefix == '' else ')'

            arg_ref = prefix + ref + fmt_argument_as_name(spec, arg, arg_nr) + suffix

            args.append(arg_ref)

            if arg.array is ArrayArgument.ARRAY:
                array_len_arg_name = fmt_argument_as_name(spec,
                        signature.args[array_len_arg_nr], array_len_arg_nr)
                args.append('(Py_ssize_t)' + array_len_arg_name)

            args.append(backend.get_type_ref(arg.definition))

            if arg.array is not ArrayArgument.ARRAY:
                args.append('SIP_NULLPTR')

        elif arg.type is ArgumentType.CAPSULE:
            args.append('"' + arg.definition.as_cpp + '"')

        else:
            if arg.array is not ArrayArgument.ARRAY_SIZE:
                args.append(
                        '*' * nr_derefs + fmt_argument_as_name(spec, arg, arg_nr))

            if arg.array is ArrayArgument.ARRAY:
                array_len_arg_name = fmt_argument_as_name(spec,
                        signature.args[array_len_arg_nr], array_len_arg_nr)
                args.append('(Py_ssize_t)' + array_len_arg_name)
            elif arg.type is ArgumentType.ENUM and arg.definition.fq_cpp_name is not None:
                args.append(backend.get_type_ref(arg.definition))

    return ', '.join(args)


def _used_includes(sf, used):
    """ Generate the library header #include directives required by either a
    class or a module.
    """

    sf.write('\n')

    for iface_file in used:
        sf.write_code(iface_file.type_header_code)


def _module_api(backend, sf, bindings):
    """ Generate the API details for a module. """

    spec = backend.spec
    module = spec.module
    module_name = module.py_name

    for klass in spec.classes:
        if klass.iface_file.module is module:
            backend.g_class_api(sf, klass)

        if klass.export_derived:
            sf.write_code(klass.iface_file.type_header_code)
            _shadow_class_declaration(backend, sf, bindings, klass)

    for mapped_type in spec.mapped_types:
        if mapped_type.iface_file.module is module:
            backend.g_mapped_type_api(sf, mapped_type)

    no_exceptions = True

    for exception in spec.exceptions:
        if exception.iface_file.module is module and exception.exception_nr >= 0:
            if no_exceptions:
                sf.write(
f'''
/* The exceptions defined in this module. */
extern PyObject *sipExportedExceptions_{module_name}[];

''')

                no_exceptions = False

            sf.write(f'#define sipException_{exception.iface_file.fq_cpp_name.as_word} sipExportedExceptions_{module_name}[{exception.exception_nr}]\n')

    backend.g_enum_macros(sf)

    for virtual_error_handler in spec.virtual_error_handlers:
        if virtual_error_handler.module is module:
            sf.write(f'\nvoid sipVEH_{module_name}_{virtual_error_handler.name}(sipSimpleWrapper *, sip_gilstate_t);\n')


def _imported_module_api(backend, sf, imported_module):
    """ Generate the API details for an imported module. """

    spec = backend.spec
    module_name = spec.module.py_name

    for klass in spec.classes:
        iface_file = klass.iface_file

        if iface_file.module is imported_module:
            if iface_file.needed:
                type_ref = backend.get_type_ref(klass)

                if iface_file.type is IfaceFileType.NAMESPACE:
                    sf.write(f'\n#if !defined({type_ref})')

                sf.write(f'\n#define {type_ref} sipImportedTypes_{module_name}_{iface_file.module.py_name}[{iface_file.type_nr}].it_td\n')

                if iface_file.type is IfaceFileType.NAMESPACE:
                    sf.write('#endif\n')

            backend.g_enum_macros(sf, scope=klass,
                    imported_module=imported_module)

    for mapped_type in spec.mapped_types:
        iface_file = mapped_type.iface_file

        if iface_file.module is imported_module:
            if iface_file.needed:
                sf.write(f'\n#define {backend.get_type_ref(mapped_type)} sipImportedTypes_{module_name}_{iface_file.module.py_name}[{iface_file.type_nr}].it_td\n')

            backend.g_enum_macros(sf, scope=mapped_type,
                    imported_module=imported_module)

    for exception in spec.exceptions:
        iface_file = exception.iface_file

        if iface_file.module is imported_module and exception.exception_nr >= 0:
            # TODO ABI v14 will get the exception directly from the imported
            # module's state (possibly via an API call) rather than taking a
            # copy of the Python object.
            sf.write(f'\n#define sipException_{iface_file.fq_cpp_name.as_word} sipImportedExceptions_{module_name}_{iface_file.module.py_name}[{exception.exception_nr}].iexc_object\n')

    backend.g_enum_macros(sf, imported_module=imported_module)


def _shadow_class_declaration(backend, sf, bindings, klass):
    """ Generate the shadow class declaration. """

    spec = backend.spec
    klass_name = klass.iface_file.fq_cpp_name.as_word

    sf.write(
f'''

class sip{klass_name} : public {backend.scoped_class_name(klass)}
{{
public:
''')

    # Define a shadow class for any protected classes we have.
    for protected_klass in spec.classes:
        if not protected_klass.is_protected:
            continue

        # See if the class defining the class is in our class hierachy.
        for mro in klass.mro:
            if mro is protected_klass.scope:
                break
        else:
            continue

        protected_klass_base_name = protected_klass.iface_file.fq_cpp_name.base_name

        sf.write(
f'''    class sip{protected_klass_base_name} : public {protected_klass_base_name} {{
    public:
''')

        _protected_enums(sf, spec, protected_klass)

        sf.write('    };\n\n')

    # The constructor declarations.
    for ctor in _unique_class_ctors(spec, klass):
        args = fmt_signature_as_cpp_declaration(spec, ctor.cpp_signature,
                scope=klass.iface_file)
        throw_specifier = _throw_specifier(bindings, ctor.throw_args)

        sf.write(f'    sip{klass_name}({args}){throw_specifier};\n')

    # The destructor.
    if klass.dtor is not AccessSpecifier.PRIVATE:
        virtual_s = 'virtual ' if len(klass.virtual_overloads) != 0 else ''
        throw_specifier = _throw_specifier(bindings, klass.dtor_throw_args)

        sf.write(f'    {virtual_s}~sip{klass_name}(){throw_specifier};\n')

    # The metacall methods if required.
    if (backend.pyqt5_supported() or backend.pyqt6_supported()) and klass.is_qobject:
        sf.write(
'''
    int qt_metacall(QMetaObject::Call, int, void **) SIP_OVERRIDE;
    void *qt_metacast(const char *) SIP_OVERRIDE;
''')

        if not klass.pyqt_no_qmetaobject:
            sf.write('    const QMetaObject *metaObject() const SIP_OVERRIDE;\n')

    # The exposure of protected enums.
    _protected_enums(sf, spec, klass)

    # The wrapper around each protected member function.
    _protected_declarations(sf, spec, klass)

    # The catcher around each virtual function in the hierarchy.
    for virt_nr, virtual_overload in enumerate(_unique_class_virtual_overloads(spec, klass)):
        if virt_nr == 0:
            sf.write(
'''
    /*
     * There is a protected method for every virtual method visible from
     * this class.
     */
protected:
''')

        sf.write('    ')
        _overload_decl(sf, spec, bindings, klass, virtual_overload.overload)
        sf.write(';\n')

    sf.write(
'''
public:
    sipSimpleWrapper *sipPySelf;
''')

    # The private declarations.
    sf.write(
f'''
private:
    sip{klass_name}(const sip{klass_name} &);
    sip{klass_name} &operator = (const sip{klass_name} &);
''')

    nr_virtual_overloads = _count_virtual_overloads(spec, klass)
    if nr_virtual_overloads > 0:
        sf.write(f'\n    char sipPyMethods[{nr_virtual_overloads}];\n')

    sf.write('};\n')


def _overload_decl(sf, spec, bindings, klass, overload):
    """ Generate the C++ declaration for an overload. """

    cpp_signature = overload.cpp_signature

    # Counter the handling of protected enums by the argument formatter.
    protection_state = set()
    _remove_protections(cpp_signature, protection_state)

    result_type = fmt_argument_as_cpp_type(spec, cpp_signature.result,
            scope=klass.iface_file)
 
    args = []
    for arg in cpp_signature.args:
        args.append(
                fmt_argument_as_cpp_type(spec, arg, scope=klass.iface_file))

    args = ', '.join(args)
 
    const_s = ' const' if overload.is_const else ''
    throw_specifier = _throw_specifier(bindings, overload.throw_args)

    sf.write(f'{result_type} {_overload_cpp_name(overload)}({args}){const_s}{throw_specifier} SIP_OVERRIDE')

    _restore_protections(protection_state)


def _type_init(backend, sf, bindings, klass):
    """ Generate the initialisation function for the type. """

    spec = backend.spec

    # See if we need to name the self and owner arguments so that we can avoid
    # a compiler warning about an unused argument.
    need_self = (spec.c_bindings or klass.has_shadow)
    need_owner = spec.c_bindings

    for ctor in klass.ctors:
        if is_used_in_code(ctor.method_code, 'sipSelf'):
            need_self = True

        if ctor.transfer is Transfer.TRANSFER:
            need_owner = True
        else:
            for arg in ctor.py_signature.args:
                if not arg.is_in:
                    continue

                if arg.key is not None:
                    need_self = True

                if arg.transfer is Transfer.TRANSFER:
                    need_self = True

                if arg.transfer is Transfer.TRANSFER_THIS:
                    need_owner = True

    sf.write('\n\n')

    backend.g_type_init(sf, bindings, klass, need_self, need_owner)


def _count_virtual_overloads(spec, klass):
    """ Return the number of virtual members in a class. """

    return len(list(_unique_class_virtual_overloads(spec, klass)))

 
def _throw_specifier(bindings, throw_args):
    """ Return a throw specifier. """

    return ' noexcept' if bindings.exceptions and throw_args is not None and throw_args.arguments is None else ''


def _member_function(backend, sf, bindings, klass, member, original_klass):
    """ Generate a class member function. """

    spec = backend.spec

    # Check that there is at least one overload that needs to be handled.  See
    # if we can avoid naming the "self" argument (and suppress a compiler
    # warning).  See if we need to remember if "self" was explicitly passed as
    # an argument.  See if we need to handle keyword arguments.
    need_method = need_self = need_args = need_selfarg = need_orig_self = False

    for overload in original_klass.overloads:
        # Skip protected methods if we don't have the means to handle them.
        if overload.access_specifier is AccessSpecifier.PROTECTED and not klass.has_shadow:
            continue

        if not skip_overload(overload, member, klass, original_klass):
            need_method = True

            if overload.access_specifier is not AccessSpecifier.PRIVATE:
                need_args = True

                if spec.target_abi >= (13, 0) or not overload.is_static:
                    need_self = True

                    if overload.is_abstract:
                        need_orig_self = True
                    elif overload.is_virtual or overload.is_virtual_reimplementation or is_used_in_code(overload.method_code, 'sipSelfWasArg'):
                        need_selfarg = True

    # Handle the trivial case.
    if not need_method:
        return

    klass_name = klass.iface_file.fq_cpp_name.as_word
    member_py_name = member.py_name.name

    sf.write('\n\n')

    # Generate the docstrings.
    if has_method_docstring(bindings, member, original_klass.overloads):
        sf.write(f'PyDoc_STRVAR(doc_{klass_name}_{member_py_name}, "')

        has_auto_docstring = backend.g_method_docstring(sf, bindings, member,
                original_klass.overloads,
                is_method=not klass.is_hidden_namespace)

        sf.write('");\n\n')
    else:
        has_auto_docstring = False

    if member.no_arg_parser or member.allow_keyword_args:
        arg3_type = ', PyObject *'
        arg3_decl = ', PyObject *sipKwds'
    else:
        arg3_type = ''
        arg3_decl = ''

    sip_self = 'sipSelf' if need_self else ''
    sip_args = 'sipArgs' if need_args else ''

    if not spec.c_bindings:
        sf.write(f'extern "C" {{static PyObject *meth_{klass_name}_{member_py_name}(PyObject *, PyObject *{arg3_type});}}\n')

    sf.write(f'static PyObject *meth_{klass_name}_{member_py_name}(PyObject *{sip_self}, PyObject *{sip_args}{arg3_decl})\n{{\n')

    if bindings.tracing:
        sf.write(f'    sipTrace(SIP_TRACE_METHODS, "meth_{klass_name}_{member_py_name}()\\n");\n\n')

    if not member.no_arg_parser:
        backend.g_method_support_vars(sf)

        if need_args:
            sf.write('    PyObject *sipParseErr = SIP_NULLPTR;\n')

        if need_selfarg:
            # This determines if we call the explicitly scoped version or the
            # unscoped version (which will then go via the vtable).
            #
            # - If the call was unbound and self was passed as the first
            #   argument (ie. Foo.meth(self)) then we always want to call the
            #   explicitly scoped version.
            #
            # - If the call was bound then we only call the unscoped version in
            #   case there is a C++ sub-class reimplementation that Python
            #   knows nothing about.  Otherwise, if the call was invoked by
            #   super() within a Python reimplementation then the Python
            #   reimplementation would be called recursively.
            #
            # In addition, if the type is a derived class then we know that
            # there can't be a C++ sub-class that we don't know about so we can
            # avoid the vtable.
            #
            # Note that we would like to rename 'sipSelfWasArg' to
            # 'sipExplicitScope' but it is part of the public API.
            if spec.target_abi >= (13, 0):
                sipself_test = f'!PyObject_TypeCheck(sipSelf, sipTypeAsPyTypeObject({backend.get_type_ref(klass)}))'
            else:
                sipself_test = '!sipSelf'

            sf.write(f'    bool sipSelfWasArg = ({sipself_test} || sipIsDerivedClass((sipSimpleWrapper *)sipSelf));\n')

        if need_orig_self:
            # This is similar to the above but for abstract methods.  We allow
            # the (potential) recursion because it means that the concrete
            # implementation can be put in a mixin and it will all work.
            sf.write('    PyObject *sipOrigSelf = sipSelf;\n')

    for overload in original_klass.overloads:
        # If we are handling one variant then we must handle them all.
        if skip_overload(overload, member, klass, original_klass, want_local=False):
            continue

        if overload.access_specifier is AccessSpecifier.PRIVATE:
            continue

        if member.no_arg_parser:
            sf.write_code(overload.method_code)
            break

        _function_body(backend, sf, bindings, klass, overload,
                original_klass=original_klass)

    if not member.no_arg_parser:
        sip_parse_err = 'sipParseErr' if need_args else 'SIP_NULLPTR'
        klass_py_name_ref = backend.cached_name_ref(klass.py_name)
        member_py_name_ref = backend.cached_name_ref(member.py_name)
        docstring_ref = f'doc_{klass_name}_{member_py_name}' if has_auto_docstring else 'SIP_NULLPTR'

        sf.write(
f'''
    sipNoMethod({sip_parse_err}, {klass_py_name_ref}, {member_py_name_ref}, {docstring_ref});

    return SIP_NULLPTR;
''')

    sf.write('}\n')


def _function_body(backend, sf, bindings, scope, overload, original_klass=None,
        dereferenced=True):
    """ Generate the function calls for a particular overload. """

    spec = backend.spec

    if scope is None:
        original_scope = None
    elif isinstance(scope, WrappedClass):
        # If there was no original class (ie. where a virtual was first
        # defined) then use this class,
        if original_klass is None:
            original_klass = scope

        original_scope = original_klass
    else:
        original_scope = scope

    py_signature = overload.py_signature

    sf.write('\n    {\n')

    # In case we have to fiddle with it.
    py_signature_adjusted = False

    if is_number_slot(overload.common.py_slot):
        # Number slots must have two arguments because we parse them slightly
        # differently.
        if len(py_signature.args) == 1:
            py_signature.args.append(py_signature.args[0])

            # Insert self in the right place.
            py_signature.args[0] = Argument(ArgumentType.CLASS, is_in=True,
                    is_reference=True, definition=original_klass)

            py_signature_adjusted = True

        backend.g_arg_parser(sf, scope, py_signature, overload=overload)
    elif not is_int_arg_slot(overload.common.py_slot) and not is_zero_arg_slot(overload.common.py_slot):
        backend.g_arg_parser(sf, scope, py_signature, overload=overload)

    _function_call(backend, sf, bindings, scope, overload, dereferenced,
            original_scope)

    sf.write('    }\n')

    if py_signature_adjusted:
        del overload.py_signature.args[0]


def _handle_result(backend, sf, overload, is_new_instance, result_size_arg_nr,
        action):
    """ Generate the code to handle the result of a call to a member function.
    """

    spec = backend.spec
    result = overload.py_signature.result

    if result.type is ArgumentType.VOID and len(result.derefs) == 0:
        result = None

    # See if we are returning 0, 1 or more values.
    nr_return_values = 0

    if result is not None:
        only_out_arg_nr = -1
        nr_return_values += 1

    has_owner = False

    for arg_nr, arg in enumerate(overload.py_signature.args):
        if arg.is_out:
            only_out_arg_nr = arg_nr
            nr_return_values += 1

        if arg.transfer is Transfer.TRANSFER_THIS:
            has_owner = True

    # Handle the trivial case.
    if nr_return_values == 0:
        sf.write(
f'''            Py_INCREF(Py_None);
            {action} Py_None;
''')

        return

    # Handle results that are classes or mapped types separately.
    if result is not None and result.type in (ArgumentType.CLASS, ArgumentType.MAPPED):
        result_type_ref = backend.get_type_ref(result.definition)

        if overload.transfer is Transfer.TRANSFER_BACK:
            result_owner = 'Py_None'
        elif overload.transfer is Transfer.TRANSFER:
            result_owner = 'sipSelf'
        else:
            result_owner = 'SIP_NULLPTR'

        sip_res = _const_cast(spec, result, 'sipRes')

        if is_new_instance or overload.factory:
            this_action = action if nr_return_values == 1 else 'PyObject *sipResObj ='
            owner = '(PyObject *)sipOwner' if has_owner and overload.factory else result_owner

            sf.write(f'            {this_action} sipConvertFromNewType({sip_res}, {result_type_ref}, {owner});\n')

            # Shortcut if this is the only value returned.
            if nr_return_values == 1:
                return
        else:
            need_xfer = overload.transfer is Transfer.TRANSFER and overload.is_static

            this_action = 'PyObject *sipResObj =' if nr_return_values > 1 or need_xfer else action
            owner = 'SIP_NULLPTR' if need_xfer else result_owner

            sf.write(f'            {this_action} sipConvertFromType({sip_res}, {result_type_ref}, {owner});\n')

            # Transferring the result of a static overload needs an explicit
            # call to sipTransferTo().
            if need_xfer:
                sf.write('\n           sipTransferTo(sipResObj, Py_None);\n')

            # Shortcut if this is the only value returned.
            if nr_return_values == 1:
                if need_xfer:
                    sf.write('\n           return sipResObj;\n')

                return

    # If there are multiple values then build a tuple.
    if nr_return_values > 1:
        build_result_args = []

        if spec.target_abi >= (14, 0):
            build_result_args.append('sipModule')

        build_result_args.append('SIP_NULLPTR')

        # Build the format string.
        format_s = ''

        if result is not None:
            format_s += 'R' if result.type in (ArgumentType.CLASS, ArgumentType.MAPPED) else _get_build_result_format(result)

        for arg in overload.py_signature.args:
            if arg.is_out:
                format_s += _get_build_result_format(arg)

        build_result_args.append('"(' + format_s + ')"')

        # Pass the values for conversion.
        if result is not None:
            build_result_args.append('sipResObj' if result.type in (ArgumentType.CLASS, ArgumentType.MAPPED) else 'sipRes')

            if result.type is ArgumentType.ENUM and result.definition.fq_cpp_name is not None:
                build_result_args.append(
                        backend.get_type_ref(result.definition))

        for arg_nr, arg in enumerate(overload.py_signature.args):
            if arg.is_out:
                build_result_args.append(fmt_argument_as_name(spec, arg,
                        arg_nr))

                if arg.type in (ArgumentType.CLASS, ArgumentType.MAPPED):
                    build_result_args.append(
                            backend.get_type_ref(arg.definition))

                    transfer = 'Py_None' if arg.transfer is Transfer.TRANSFER_BACK else 'SIP_NULLPTR'
                    build_result_args.append(transfer)
                elif arg.type is ArgumentType.ENUM and arg.definition.fq_cpp_name is not None:
                    build_result_args.append(
                            backend.get_type_ref(arg.definition))

        build_result_args = ', '.join(build_result_args)

        sf.write(f'            {action} sipBuildResult({build_result_args});\n')

        # All done for multiple values.
        return

    # Deal with the only returned value.
    if only_out_arg_nr < 0:
        value = result
        value_name = 'sipRes'
    else:
        value = overload.py_signature.args[only_out_arg_nr]
        value_name = fmt_argument_as_name(spec, value, only_out_arg_nr)

    if value.type in (ArgumentType.CLASS, ArgumentType.MAPPED):
        need_new_instance = _need_new_instance(value)

        convertor = 'sipConvertFromNewType' if need_new_instance else 'sipConvertFromType'
        value_name = _const_cast(spec, value, value_name)
        transfer = 'Py_None' if not need_new_instance and value.transfer is Transfer.TRANSFER_BACK else 'SIP_NULLPTR'

        sf.write(f'            {action} {convertor}({value_name}, {backend.get_type_ref(value.definition)}, {transfer});\n')

    elif value.type is ArgumentType.ENUM:
        if value.definition.fq_cpp_name is not None:
            if not spec.c_bindings:
                value_name = f'static_cast<int>({value_name})'

            sf.write(f'            {action} sipConvertFromEnum({value_name}, {backend.get_type_ref(value.definition)});\n')
        else:
            sf.write(f'            {action} PyLong_FromLong({value_name});\n')

    elif value.type is ArgumentType.ASCII_STRING:
        if len(value.derefs) == 0:
            sf.write(f'            {action} PyUnicode_DecodeASCII(&{value_name}, 1, SIP_NULLPTR);\n')
        else:
            sf.write(
f'''            if ({value_name} == SIP_NULLPTR)
            {{
                Py_INCREF(Py_None);
                return Py_None;
            }}

            {action} PyUnicode_DecodeASCII({value_name}, strlen({value_name}), SIP_NULLPTR);
''')

    elif value.type is ArgumentType.LATIN1_STRING:
        if len(value.derefs) == 0:
            sf.write(f'            {action} PyUnicode_DecodeLatin1(&{value_name}, 1, SIP_NULLPTR);\n')
        else:
            sf.write(
f'''            if ({value_name} == SIP_NULLPTR)
            {{
                Py_INCREF(Py_None);
                return Py_None;
            }}

            {action} PyUnicode_DecodeLatin1({value_name}, strlen({value_name}), SIP_NULLPTR);
''')

    elif value.type is ArgumentType.UTF8_STRING:
        if len(value.derefs) == 0:
            sf.write(f'            {action} PyUnicode_FromStringAndSize(&{value_name}, 1);\n')
        else:
            sf.write(
f'''            if ({value_name} == SIP_NULLPTR)
            {{
                Py_INCREF(Py_None);
                return Py_None;
            }}

            {action} PyUnicode_FromString({value_name});
''')

    elif value.type in (ArgumentType.SSTRING, ArgumentType.USTRING, ArgumentType.STRING):
        cast = '' if value.type is ArgumentType.STRING else '(char *)'

        if len(value.derefs) == 0:
            sf.write(f'            {action} PyBytes_FromStringAndSize({cast}&{value_name}, 1);\n')
        else:
            sf.write(
f'''            if ({value_name} == SIP_NULLPTR)
            {{
                Py_INCREF(Py_None);
                return Py_None;
            }}

            {action} PyBytes_FromString({cast}{value_name});
''')

    elif value.type is ArgumentType.WSTRING:
        if len(value.derefs) == 0:
            sf.write(f'            {action} PyUnicode_FromWideChar(&{value_name}, 1);\n')
        else:
            sf.write(
f'''            if ({value_name} == SIP_NULLPTR)
            {{
                Py_INCREF(Py_None);
                return Py_None;
            }}

            {action} PyUnicode_FromWideChar({value_name}, (Py_ssize_t)wcslen({value_name}));
''')

    elif value.type in (ArgumentType.BOOL, ArgumentType.CBOOL):
        # TODO v14 and C uses _Bool.
        sf.write(f'            {action} PyBool_FromLong({value_name});\n')

    elif value.type in (ArgumentType.BYTE, ArgumentType.SBYTE, ArgumentType.SHORT, ArgumentType.INT, ArgumentType.CINT, ArgumentType.LONG):
        sf.write(f'            {action} PyLong_FromLong({value_name});\n')

    elif value.type in (ArgumentType.UBYTE, ArgumentType.USHORT, ArgumentType.UINT, ArgumentType.ULONG, ArgumentType.SIZE):
        sf.write(f'            {action} PyLong_FromUnsignedLong({value_name});\n')

    elif value.type is ArgumentType.LONGLONG:
        sf.write(f'            {action} PyLong_FromLongLong({value_name});\n')

    elif value.type is ArgumentType.ULONGLONG:
        sf.write(f'            {action} PyLong_FromUnsignedLongLong({value_name});\n')

    elif value.type is ArgumentType.SSIZE:
        sf.write(f'            {action} PyLong_FromSsize_t({value_name});\n')

    elif value.type is ArgumentType.VOID:
        convertor = 'sipConvertFromConstVoidPtr' if value.is_const else 'sipConvertFromVoidPtr'
        if result_size_arg_nr >= 0:
            convertor += 'AndSize'

        sf.write(f'            {action} {convertor}({_get_void_ptr_cast(value)}{value_name}')

        if result_size_arg_nr >= 0:
            sf.write(', ' + fmt_argument_as_name(spec, overload.py_signature.args[result_size_arg_nr], result_size_arg_nr))

        sf.write(');\n')

    elif value.type is ArgumentType.CAPSULE:
        sf.write(f'            {action} PyCapsule_New({value_name}, "{value.definition.as_cpp}", SIP_NULLPTR);\n')

    elif value.type in (ArgumentType.STRUCT, ArgumentType.UNION):
        convertor = 'sipConvertFromConstVoidPtr' if value.is_const else 'sipConvertFromVoidPtr'

        sf.write(f'            {action} {convertor}({value_name});\n')

    elif value.type in (ArgumentType.FLOAT, ArgumentType.CFLOAT):
        sf.write(f'            {action} PyFloat_FromDouble((double){value_name});\n')

    elif value.type in (ArgumentType.DOUBLE, ArgumentType.CDOUBLE):
        sf.write(f'            {action} PyFloat_FromDouble({value_name});\n')

    elif value.type in (ArgumentType.PYOBJECT, ArgumentType.PYTUPLE, ArgumentType.PYLIST, ArgumentType.PYDICT, ArgumentType.PYCALLABLE, ArgumentType.PYSLICE, ArgumentType.PYTYPE, ArgumentType.PYBUFFER, ArgumentType.PYENUM):
        sf.write(f'            {action} {value_name};\n')


def _get_build_result_format(type):
    """ Return the format string used by sipBuildResult() for a particular
    type.
    """

    if type.type in (ArgumentType.CLASS, ArgumentType.MAPPED):
        return 'N' if _need_new_instance(type) else 'D'

    if type.type is ArgumentType.FAKE_VOID:
        return 'D'

    if type.type in (ArgumentType.BOOL, ArgumentType.CBOOL):
        return 'b'

    if type.type in (ArgumentType.ASCII_STRING, ArgumentType.LATIN1_STRING, ArgumentType.UTF8_STRING):
        return 'A' if _is_string(type) else 'a'

    if type.type in (ArgumentType.SSTRING, ArgumentType.USTRING, ArgumentType.STRING):
        return 's' if _is_string(type) else 'c'

    if type.type is ArgumentType.WSTRING:
        return 'x' if _is_string(type) else 'w'

    if type.type is ArgumentType.ENUM:
        return 'F' if type.definition.fq_cpp_name is not None else 'e'

    if type.type in (ArgumentType.BYTE, ArgumentType.SBYTE):
        # Note that this is the correct thing to do even if char is unsigned.
        return 'L'

    if type.type is ArgumentType.UBYTE:
        return 'M'

    if type.type is ArgumentType.SHORT:
        return 'h'

    if type.type is ArgumentType.USHORT:
        return 't'

    if type.type in (ArgumentType.INT, ArgumentType.CINT):
        return 'i'

    if type.type is ArgumentType.UINT:
        return 'u'

    if type.type is ArgumentType.SIZE:
        return '='

    if type.type is ArgumentType.LONG:
        return 'l'

    if type.type is ArgumentType.ULONG:
        return 'm'

    if type.type is ArgumentType.LONGLONG:
        return 'n'

    if type.type is ArgumentType.ULONGLONG:
        return 'o'

    if type.type in (ArgumentType.STRUCT, ArgumentType.UNION, ArgumentType.VOID):
        return 'V'

    if type.type is ArgumentType.CAPSULE:
        return 'z'

    if type.type in (ArgumentType.FLOAT, ArgumentType.CFLOAT):
        return 'f'

    if type.type in (ArgumentType.DOUBLE, ArgumentType.CDOUBLE):
        return 'd'

    if type.type in (ArgumentType.PYOBJECT, ArgumentType.PYTUPLE, ArgumentType.PYLIST, ArgumentType.PYDICT, ArgumentType.PYCALLABLE, ArgumentType.PYSLICE, ArgumentType.PYTYPE, ArgumentType.PYBUFFER, ArgumentType.PYENUM):
        return 'R'

    # We should never get here.
    return ''


def _is_string(type):
    """ Check if a type is a string rather than a char type. """

    nr_derefs = len(type.derefs)

    if type.is_out and not type.is_reference:
        nr_derefs -= 1

    return nr_derefs > 0


def _needs_heap_copy(arg, using_copy_ctor=True):
    """ Return True if an argument (or result) needs to be copied to the heap.
    """

    # The type is a class or mapped type and not a pointer.
    if not arg.no_copy and arg.type in (ArgumentType.CLASS, ArgumentType.MAPPED) and len(arg.derefs) == 0:
        # We need a copy unless it is a non-const reference.
        if not arg.is_reference or arg.is_const:
            # We assume we can copy a mapped type.
            if arg.type is ArgumentType.MAPPED:
                return True

            klass = arg.definition

            # We can't copy an abstract class.
            if klass.is_abstract:
                return False

            # We can copy if we have a public copy ctor.
            if not klass.cannot_copy:
                return True

            # We can't copy if we must use a copy ctor.
            if using_copy_ctor:
                return False

            # We can copy if we have a public assignment operator.
            return not klass.cannot_assign

    return False


def _function_call(backend, sf, bindings, scope, overload, dereferenced,
        original_scope):
    """ Generate a function call. """

    spec = backend.spec
    py_slot = overload.common.py_slot
    result = overload.py_signature.result
    result_cpp_type = fmt_argument_as_cpp_type(spec, result, plain=True,
            no_derefs=True)
    static_factory = (scope is None or overload.is_static) and overload.factory

    sf.write('        {\n')

    # If there is no shadow class then protected methods can never be called.
    if overload.access_specifier is AccessSpecifier.PROTECTED and not scope.has_shadow:
        sf.write(
'''            /* Never reached. */
        }
''')

        return

    # Save the full result type as we may want to fiddle with it.
    saved_result_is_const = result.is_const

    # See if we need to make a copy of the result on the heap.
    is_new_instance = _needs_heap_copy(result, using_copy_ctor=False)

    if is_new_instance:
        result.is_const = False

    result_decl = _get_result_decl(backend, scope, overload, result)
    if result_decl is not None:
        sf.write('            ' + result_decl + ';\n')
        separating_newline = True
    else:
        separating_newline = False

    # See if we want to keep a reference to the result.
    post_process = result.key is not None

    delete_temporaries = True
    result_size_arg_nr = -1

    for arg_nr, arg in enumerate(overload.py_signature.args):
        if arg.result_size:
            result_size_arg_nr = arg_nr

        if static_factory and arg.key is not None:
            post_process = True

        # If we have an In,Out argument that has conversion code then we delay
        # the destruction of any temporary variables until after we have
        # converted the outputs.
        if arg.is_in and arg.is_out and get_convert_to_type_code(arg) is not None:
            delete_temporaries = False
            post_process = True

        # If we are returning a class via an output only reference or pointer
        # then we need an instance on the heap.
        if arg.type in (ArgumentType.CLASS, ArgumentType.MAPPED) and _need_new_instance(arg):
            arg_name = fmt_argument_as_name(spec, arg, arg_nr)
            arg_cpp_type = fmt_argument_as_cpp_type(spec, arg, plain=True,
                    no_derefs=True)
            sf.write(f'            {arg_name} = new {arg_cpp_type}();\n')
            separating_newline = True

    if post_process:
        sf.write('            PyObject *sipResObj;\n')
        separating_newline = True

    if overload.premethod_code is not None:
        sf.write('\n')
        sf.write_code(overload.premethod_code)

    error_flag = old_error_flag = False

    if overload.method_code is not None:
        # See if the handwritten code seems to be using the error flag.
        if need_error_flag(overload.method_code):
            sf.write('            sipErrorState sipError = sipErrorNone;\n')
            error_flag = True
            separating_newline = True
        elif backend.need_deprecated_error_flag(overload.method_code):
            sf.write('            int sipIsErr = 0;\n')
            old_error_flag = True
            separating_newline = True

    if separating_newline:
        sf.write('\n')

    # If it is abstract make sure that self was bound.
    if overload.is_abstract:
        sf.write(
f'''            if (!sipOrigSelf)
            {{
                sipAbstractMethod({backend.cached_name_ref(scope.py_name)}, {backend.cached_name_ref(overload.common.py_name)});
                return SIP_NULLPTR;
            }}

''')

    if overload.deprecated is not None:
        scope_py_name_ref = backend.cached_name_ref(scope.py_name) if scope is not None and scope.py_name is not None else 'SIP_NULLPTR'
        error_return = '-1' if is_void_return_slot(py_slot) or is_int_return_slot(py_slot) or is_ssize_return_slot(py_slot) or is_hash_return_slot(py_slot) else 'SIP_NULLPTR'

        # Note that any temporaries will leak if an exception is raised.
        if backend.abi_has_deprecated_message():
            str_deprecated_message = f'"{overload.deprecated}"' if overload.deprecated else 'SIP_NULLPTR'
            sf.write(f'            if (sipDeprecated({scope_py_name_ref}, {backend.cached_name_ref(overload.common.py_name)}, {str_deprecated_message}) < 0)\n')
        else:
            sf.write(f'            if (sipDeprecated({scope_py_name_ref}, {backend.cached_name_ref(overload.common.py_name)}) < 0)\n')
        
        sf.write(f'                return {error_return};\n\n')

    # Call any pre-hook.
    if overload.prehook is not None:
        sf.write(f'            sipCallHook("{overload.prehook}");\n\n')

    if overload.method_code is not None:
        sf.write_code(overload.method_code)
    else:
        rel_gil = release_gil(overload.gil_action, bindings)
        needs_closing_paren = False

        if is_new_instance and spec.c_bindings:
            sf.write(
f'''            if ((sipRes = ({result_cpp_type} *)sipMalloc(sizeof ({result_cpp_type}))) == SIP_NULLPTR)
        {{
''')

            backend.g_gc_ellipsis(sf, overload.py_signature)

            sf.write(
'''                return SIP_NULLPTR;
            }

''')

        if overload.raises_py_exception:
            sf.write('            PyErr_Clear();\n\n')

        if isinstance(scope, WrappedClass) and scope.len_cpp_name is not None:
            _sequence_support(sf, spec, scope, overload)

        if rel_gil:
            sf.write('            Py_BEGIN_ALLOW_THREADS\n')

        backend.g_try(sf, bindings, overload.throw_args)

        sf.write('            ')

        if result_decl is not None:
            # Construct a copy on the heap if needed.
            if is_new_instance:
                if spec.c_bindings:
                    sf.write('*sipRes = ')
                elif result.type is ArgumentType.CLASS and result.definition.cannot_copy:
                    sf.write(f'sipRes = reinterpret_cast<{result_cpp_type} *>(::operator new(sizeof ({result_cpp_type})));\n            *sipRes = ')
                else:
                    sf.write(f'sipRes = new {result_cpp_type}(')
                    needs_closing_paren = True
            else:
                sf.write('sipRes = ')

                # See if we need the address of the result.  Any reference will
                # be non-const.
                if result.type in (ArgumentType.CLASS, ArgumentType.MAPPED) and (len(result.derefs) == 0 or result.is_reference):
                    sf.write('&')

        if py_slot is None:
            _cpp_function_call(backend, sf, scope, overload, original_scope)
        elif py_slot is PySlot.CALL:
            sf.write('(*sipCpp)(')
            backend.g_call_args(sf, overload.cpp_signature,
                    overload.py_signature)
            sf.write(')')
        else:
            sf.write(_get_slot_call(backend, scope, overload, dereferenced))

        if needs_closing_paren:
            sf.write(')')

        sf.write(';\n')

        backend.g_catch(sf, bindings, overload.py_signature,
                overload.throw_args, rel_gil)

        if rel_gil:
            sf.write('            Py_END_ALLOW_THREADS\n')

    for arg_nr, arg in enumerate(overload.py_signature.args):
        if not arg.is_in:
            continue

        # Handle any /KeepReference/ arguments except for static factories.
        if not static_factory and arg.key is not None:
            sip_self = 'SIP_NULLPTR' if scope is None or overload.is_static else 'sipSelf'
            keep_reference_call = _get_keep_reference_call(backend, arg,
                    arg_nr, sip_self)

            sf.write(f'\n            {keep_reference_call};\n')

        # Handle /TransferThis/ for non-factory methods.
        if not overload.factory and arg.transfer is Transfer.TRANSFER_THIS:
            sf.write(
'''
            if (sipOwner)
                sipTransferTo(sipSelf, (PyObject *)sipOwner);
            else
                sipTransferBack(sipSelf);
''')

    if overload.transfer is Transfer.TRANSFER_THIS:
        sf.write('\n            sipTransferTo(sipSelf, SIP_NULLPTR);\n')

    backend.g_gc_ellipsis(sf, overload.py_signature)

    if delete_temporaries and not is_zero_arg_slot(py_slot):
        backend.g_delete_temporaries(sf, overload.py_signature)

    sf.write('\n')

    # Handle the error flag if it was used.
    error_value = '-1' if is_void_return_slot(py_slot) or is_int_return_slot(py_slot) or is_ssize_return_slot(py_slot) or is_hash_return_slot(py_slot) else '0'

    if overload.raises_py_exception:
        sf.write(
f'''            if (PyErr_Occurred())
                return {error_value};

''')
    elif error_flag:
        if not is_zero_arg_slot(py_slot):
            sf.write(
f'''            if (sipError == sipErrorFail)
                return {error_value};

''')

        sf.write(
'''            if (sipError == sipErrorNone)
            {
''')
    elif old_error_flag:
        sf.write(
f'''            if (sipIsErr)
                return {error_value};

''')

    # Call any post-hook.
    if overload.posthook is not None:
        sf.write(f'\n            sipCallHook("{overload.posthook}");\n')

    if is_void_return_slot(py_slot):
        sf.write(
'''            return 0;
''')
    elif is_inplace_number_slot(py_slot) or is_inplace_sequence_slot(py_slot):
        sf.write(
'''            Py_INCREF(sipSelf);
            return sipSelf;
''')
    elif is_int_return_slot(py_slot) or is_ssize_return_slot(py_slot) or is_hash_return_slot(py_slot):
        sf.write(
'''            return sipRes;
''')
    else:
        action = 'sipResObj =' if post_process else 'return'
        _handle_result(backend, sf, overload, is_new_instance,
                result_size_arg_nr, action)

        # Delete the temporaries now if we haven't already done so.
        if not delete_temporaries:
            backend.g_delete_temporaries(sf, overload.py_signature)

        # Keep a reference to a pointer to a class if it isn't owned by Python.
        if result.key is not None:
            sip_self = 'SIP_NULLPTR' if overload.is_static else 'sipSelf'
            sf.write(f'\n            sipKeepReference({sip_self}, {result.key}, sipResObj);\n')

        # Keep a reference to any argument with the result if the function is a
        # static factory.
        if static_factory:
            for arg_nr, arg in enumerate(overload.py_signature.args):
                if not arg.is_in:
                    continue

                if arg.key != None:
                    keep_reference_call = _get_keep_reference_call(backend,
                            arg, arg_nr, 'sipResObj')
                    sf.write(f'\n            {keep_reference_call};\n')

        if post_process:
            sf.write('\n            return sipResObj;\n')

    if error_flag:
        sf.write('            }\n')

        if not is_zero_arg_slot(py_slot):
            sf.write('\n            sipAddException(sipError, &sipParseErr);\n')

    sf.write('        }\n')

    # Restore the full state of the result.
    result.is_const = saved_result_is_const


def _get_keep_reference_call(backend, arg, arg_nr, object_name):
    """ Return a call to sipKeepReference() for an argument. """

    spec = backend.spec
    arg_name = fmt_argument_as_name(spec, arg, arg_nr)
    suffix = 'Wrapper' if arg.get_wrapper and (arg.type not in (ArgumentType.ASCII_STRING, ArgumentType.LATIN1_STRING, ArgumentType.UTF8_STRING) or len(arg.derefs) != 1) else 'Keep'

    return f'sipKeepReference({object_name}, {arg.key}, {arg_name}{suffix})'


def _get_result_decl(backend, scope, overload, result):
    """ Return the declaration of a variable to hold the result of a function
    call if one is needed.
    """

    # See if sipRes is needed.
    no_result = (is_inplace_number_slot(overload.common.py_slot) or
             is_inplace_sequence_slot(overload.common.py_slot) or
             (result.type is ArgumentType.VOID and len(result.derefs) == 0))

    if no_result:
        return None

    result_decl = backend.get_named_value_decl(scope, result, 'sipRes')

    # The typical %MethodCode usually causes a compiler warning, so we
    # initialise the result in that case to try and suppress it.
    initial_value = ' = ' + _cast_zero(backend, result) if overload.method_code is not None else ''

    return result_decl + initial_value


def _get_slot_call(backend, scope, overload, dereferenced):
    """ Return the call to a Python slot (except for PySlot.CALL which is
    handled separately).
    """

    spec = backend.spec
    py_slot = overload.common.py_slot

    if py_slot is PySlot.GETITEM:
        return f'(*sipCpp)[{_get_slot_arg(spec, overload, 0)}]'

    if py_slot in (PySlot.INT, PySlot.FLOAT):
        return '*sipCpp'

    if py_slot is PySlot.ADD:
        return _get_number_slot_call(spec, overload, '+')

    if py_slot is PySlot.CONCAT:
        return _get_binary_slot_call(backend, scope, overload, '+',
                dereferenced)

    if py_slot is PySlot.SUB:
        return _get_number_slot_call(spec, overload, '-')

    if py_slot in (PySlot.MUL, PySlot.MATMUL):
        return _get_number_slot_call(spec, overload, '*')

    if py_slot is PySlot.REPEAT:
        return _get_binary_slot_call(backend, scope, overload, '*',
                dereferenced)

    if py_slot is PySlot.TRUEDIV:
        return _get_number_slot_call(spec, overload, '/')

    if py_slot is PySlot.MOD:
        return _get_number_slot_call(spec, overload, '%')

    if py_slot is PySlot.AND:
        return _get_number_slot_call(spec, overload, '&')

    if py_slot is PySlot.OR:
        return _get_number_slot_call(spec, overload, '|')

    if py_slot is PySlot.XOR:
        return _get_number_slot_call(spec, overload, '^')

    if py_slot is PySlot.LSHIFT:
        return _get_number_slot_call(spec, overload, '<<')

    if py_slot is PySlot.RSHIFT:
        return _get_number_slot_call(spec, overload, '>>')

    if py_slot in (PySlot.IADD, PySlot.ICONCAT):
        return _get_binary_slot_call(backend, scope, overload, '+=',
                dereferenced)

    if py_slot is PySlot.ISUB:
        return _get_binary_slot_call(backend, scope, overload, '-=',
                dereferenced)

    if py_slot in (PySlot.IMUL, PySlot.IREPEAT, PySlot.IMATMUL):
        return _get_binary_slot_call(backend, scope, overload, '*=',
                dereferenced)

    if py_slot is PySlot.ITRUEDIV:
        return _get_binary_slot_call(backend, scope, overload, '/=',
                dereferenced)

    if py_slot is PySlot.IMOD:
        return _get_binary_slot_call(backend, scope, overload, '%=',
                dereferenced)

    if py_slot is PySlot.IAND:
        return _get_binary_slot_call(backend, scope, overload, '&=',
                dereferenced)

    if py_slot is PySlot.IOR:
        return _get_binary_slot_call(backend, scope, overload, '|=',
                dereferenced)

    if py_slot is PySlot.IXOR:
        return _get_binary_slot_call(backend, scope, overload, '^=',
                dereferenced)

    if py_slot is PySlot.ILSHIFT:
        return _get_binary_slot_call(backend, scope, overload, '<<=',
                dereferenced)

    if py_slot is PySlot.IRSHIFT:
        return _get_binary_slot_call(backend, scope, overload, '>>=',
                dereferenced)

    if py_slot is PySlot.INVERT:
        return '~(*sipCpp)'

    if py_slot is PySlot.LT:
        return _get_binary_slot_call(backend, scope, overload, '<',
                dereferenced)

    if py_slot is PySlot.LE:
        return _get_binary_slot_call(backend, scope, overload, '<=',
                dereferenced)

    if py_slot is PySlot.EQ:
        return _get_binary_slot_call(backend, scope, overload, '==',
                dereferenced)

    if py_slot is PySlot.NE:
        return _get_binary_slot_call(backend, scope, overload, '!=',
                dereferenced)

    if py_slot is PySlot.GT:
        return _get_binary_slot_call(backend, scope, overload, '>',
                dereferenced)

    if py_slot is PySlot.GE:
        return _get_binary_slot_call(backend, scope, overload, '>=',
                dereferenced)

    if py_slot is PySlot.NEG:
        return '-(*sipCpp)'

    if py_slot is PySlot.POS:
        return '+(*sipCpp)'

    # We should never get here.
    return ''


def _cpp_function_call(backend, sf, scope, overload, original_scope):
    """ Generate a call to a C++ function. """

    cpp_name = overload.cpp_name

    # If the function is protected then call the public wrapper.  If it is
    # virtual then call the explicit scoped function if "self" was passed as
    # the first argument.

    nr_parens = 1

    if scope is None:
        sf.write(cpp_name + '(')
    elif scope.iface_file.type is IfaceFileType.NAMESPACE:
        sf.write(f'{scope.iface_file.fq_cpp_name.as_cpp}::{cpp_name}(')
    elif overload.is_static:
        if overload.access_specifier is AccessSpecifier.PROTECTED:
            sf.write(f'sip{scope.iface_file.fq_cpp_name.as_word}::sipProtect_{cpp_name}(')
        else:
            sf.write(f'{original_scope.iface_file.fq_cpp_name.as_cpp}::{cpp_name}(')
    elif overload.access_specifier is AccessSpecifier.PROTECTED:
        if not overload.is_abstract and (overload.is_virtual or overload.is_virtual_reimplementation):
            sf.write(f'sipCpp->sipProtectVirt_{cpp_name}(sipSelfWasArg')

            if len(overload.cpp_signature.args) != 0:
                sf.write(', ')
        else:
            sf.write(f'sipCpp->sipProtect_{cpp_name}(')
    elif not overload.is_abstract and (overload.is_virtual or overload.is_virtual_reimplementation):
        sf.write(f'(sipSelfWasArg ? sipCpp->{original_scope.iface_file.fq_cpp_name.as_cpp}::{cpp_name}(')
        backend.g_call_args(sf, overload.cpp_signature, overload.py_signature)
        sf.write(f') : sipCpp->{cpp_name}(')
        nr_parens += 1
    else:
        sf.write(f'sipCpp->{cpp_name}(')

    backend.g_call_args(sf, overload.cpp_signature, overload.py_signature)

    sf.write(')' * nr_parens)


def _get_slot_arg(spec, overload, arg_nr):
    """ Return an argument to a slot call. """

    arg = overload.py_signature.args[arg_nr]

    prefix = suffix = ''

    if arg.type in (ArgumentType.CLASS, ArgumentType.MAPPED):
        if len(arg.derefs) == 0:
            prefix = '*'
    elif arg_is_small_enum(arg):
        prefix = 'static_cast<' + fmt_enum_as_cpp_type(arg.definition) + '>('
        suffix = ')'

    return prefix + fmt_argument_as_name(spec, arg, arg_nr) + suffix


# A map of operators and their complements.
_OPERATOR_COMPLEMENTS = {
    '<': '>=',
    '<=': '>',
    '==': '!=',
    '!=': '==',
    '>': '<=',
    '>=': '<',
}

def _get_binary_slot_call(backend, scope, overload, operator, dereferenced):
    """ Return the call to a binary (non-number) slot method. """

    slot_call = ''

    if overload.is_complementary:
        operator = _OPERATOR_COMPLEMENTS[operator]
        slot_call += '!'

    if overload.is_global:
        # If it has been moved from a namespace then get the C++ scope.
        if overload.common.namespace_iface_file is not None:
            slot_call += overload.common.namespace_iface_file.fq_cpp_name.as_cpp + '::'

        if dereferenced:
            slot_call += f'operator{operator}((*sipCpp), '
        else:
            slot_call += f'operator{operator}(sipCpp, '
    else:
        dereference = '->' if dereferenced else '.'

        if overload.is_abstract:
            slot_call += f'sipCpp{dereference}operator{operator}('
        else:
            cpp_name = backend.scoped_class_name(scope)
            slot_call += f'sipCpp{dereference}{cpp_name}::operator{operator}('

    slot_call += _get_slot_arg(backend.spec, overload, 0)
    slot_call += ')'

    return slot_call


def _get_number_slot_call(spec, overload, operator):
    """ Return the call to a binary number slot method. """

    arg0 = _get_slot_arg(spec, overload, 0)
    arg1 = _get_slot_arg(spec, overload, 1)

    return f'({arg0} {operator} {arg1})'


def _need_new_instance(arg):
    """ Return True if the argument type means an instance needs to be created
    on the heap to pass back to Python.
    """

    if not arg.is_in and arg.is_out:
        if arg.is_reference and len(arg.derefs) == 0:
            return True

        if not arg.is_reference and len(arg.derefs) == 1:
            return True

    return False


def _fake_protected_args(signature):
    """ Convert any protected arguments (ie. those whose type is unavailable
    outside of a shadow class) to a fundamental type to be used instead (with
    suitable casts).
    """

    protection_state = []

    for arg in signature.args:
        if arg.type is ArgumentType.ENUM and arg.definition.is_protected:
            protection_state.append(arg)
            arg.type = ArgumentType.INT
        elif arg.type is ArgumentType.CLASS and arg.definition.is_protected:
            protection_state.append((arg, arg.derefs, arg.is_reference))
            arg.type = ArgumentType.FAKE_VOID
            arg.derefs = [False]
            arg.is_reference = False

    return protection_state


def _restore_protected_args(protection_state):
    """ Restore any protected arguments faked by _fake_protected_args(). """

    for protected in protection_state:
        if isinstance(protected, Argument):
            protected.type = ArgumentType.ENUM
        else:
            protected, derefs, is_reference = protected
            protected.type = ArgumentType.CLASS
            protected.derefs = derefs
            protected.is_reference = is_reference


def _remove_protection(arg, protection_state):
    """ Reset and save any protections so that the argument will be rendered
    exactly as defined in C++.
    """

    if arg.type in (ArgumentType.CLASS, ArgumentType.ENUM) and arg.definition.is_protected:
        arg.definition.is_protected = False
        protection_state.add(arg.definition)


def _remove_protections(signature, protection_state):
    """ Reset and save any protections so that the signature will be rendered
    exactly as defined in C++.
    """

    _remove_protection(signature.result, protection_state)

    for arg in signature.args:
        _remove_protection(arg, protection_state)


def _restore_protections(protection_state):
    """ Restore any protections removed by _remove_protection(). """

    for protected in protection_state:
        protected.is_protected = True


def _arg_name(spec, name, code):
    """ Return the argument name to use in a function definition for
    handwritten code.
    """

    # Always use the name in C code.
    if spec.c_bindings:
        return name

    # Use the name if it is used in the handwritten code.
    if is_used_in_code(code, name):
        return name

    # Don't use the name.
    return ''


def _use_in_code(code, s, spec=None):
    """ Return the string to use depending on whether it is used in some code
    and optionally if the bindings are for C.
    """

    # Always use the string for C bindings.
    if spec is not None and spec.c_bindings:
        return s

    return s if is_used_in_code(code, s) else ''


def _class_from_void(backend, klass):
    """ Return an assignment statement from a void * variable to a class
    instance variable.
    """

    klass_type = backend.scoped_class_name(klass)

    if backend.spec.c_bindings:
        return f'{klass_type} *sipCpp = ({klass_type} *)sipCppV'

    return f'{klass_type} *sipCpp = reinterpret_cast<{klass_type} *>(sipCppV)'


def _mapped_type_from_void(spec, mapped_type_type):
    """ Return an assignment statement from a void * variable to a mapped type
    variable.
    """

    if spec.c_bindings:
        return f'{mapped_type_type} *sipCpp = ({mapped_type_type} *)sipCppV'

    return f'{mapped_type_type} *sipCpp = reinterpret_cast<{mapped_type_type} *>(sipCppV)'


def _get_encoding(type):
    """ Return the encoding character for the given type. """

    if type.type is ArgumentType.ASCII_STRING:
        encoding = 'A'
    elif type.type is ArgumentType.LATIN1_STRING:
        encoding = 'L'
    elif type.type is ArgumentType.UTF8_STRING:
        encoding = '8'
    elif type.type is ArgumentType.WSTRING:
        encoding = 'w' if len(type.derefs) == 0 else 'W'
    else:
        encoding = 'N'

    return encoding


def _get_void_ptr_cast(type):
    """ Return a void* cast for an argument if needed. """

    # Generate a cast if the argument's type was a typedef.  This allows us to
    # use typedef's to void* to hide something more complex that we don't
    # handle.
    if type.original_typedef is None:
        return ''

    return '(const void *)' if type.is_const else '(void *)'


def _declare_limited_api(sf, py_debug, module=None):
    """ Declare the use of the limited API. """

    if py_debug:
        return

    if module is None or module.use_limited_api:
        sf.write(
'''
#if !defined(Py_LIMITED_API)
#define Py_LIMITED_API
#endif
''')


def _include_sip_h(sf, module):
    """ Generate the inclusion of sip.h. """

    if module.py_ssize_t_clean:
        sf.write(
'''
#define PY_SSIZE_T_CLEAN
''')

    sf.write(
'''
#include "sip.h"
''')


def _exception_handler(backend, sf):
    """ Generate the exception handler for a module. """

    spec = backend.spec
    need_decl = True

    for exception in spec.exceptions:
        if exception.iface_file.module is spec.module:
            if need_decl:
                sf.write(
f'''

/* Handle the exceptions defined in this module. */
bool sipExceptionHandler_{spec.module.py_name}(std::exception_ptr sipExcPtr)
{{
    try {{
        std::rethrow_exception(sipExcPtr);
    }}
''')

                need_decl = False

            backend.g_catch_block(sf, exception)

    if not need_decl:
        sf.write(
'''    catch (...) {}

    return false;
}
''')
 
 
def _append_qualifier_defines(module, bindings, qualifier_defines):
    """ Append the #defines for each feature defined in a module to a list of
    them.
    """

    for qualifier in module.qualifiers:
        qualifier_type_name = None

        if qualifier.type is QualifierType.TIME:
            if _qualifier_enabled(qualifier, bindings):
                qualifier_type_name = 'TIMELINE'

        elif qualifier.type is QualifierType.PLATFORM:
            if _qualifier_enabled(qualifier, bindings):
                qualifier_type_name = 'PLATFORM'

        elif qualifier.type is QualifierType.FEATURE:
            if qualifier.name not in bindings.disabled_features and qualifier.enabled_by_default:
                qualifier_type_name = 'FEATURE'

        if qualifier_type_name is not None:
            qualifier_defines.append(f'#define SIP_{qualifier_type_name}_{qualifier.name}')


def _qualifier_enabled(qualifier, bindings):
    """ Return True if a qualifier is enabled. """

    for tag in bindings.tags:
        if qualifier.name == tag:
            return qualifier.enabled_by_default

    return False


def _sequence_support(sf, spec, klass, overload):
    """ Generate extra support for sequences because the class has an overload
    that has been annotated with __len__.
    """

    # We require a single int argument.
    if len(overload.py_signature.args) != 1:
        return

    arg0 = overload.py_signature.args[0]

    if not py_as_int(arg0):
        return

    # At the moment all we do is check that an index to __getitem__ is within
    # range so that the class supports Python iteration.  In the future we
    # should add support for negative indices, slices, __setitem__ and
    # __delitem__ (which will require enhancements to the sip module ABI).
    if overload.common.py_slot is PySlot.GETITEM:
        index_arg = fmt_argument_as_name(spec, arg0, 0)

        sf.write(
f'''            if ({index_arg} < 0 || {index_arg} >= sipCpp->{klass.len_cpp_name}())
            {{
                PyErr_SetNone(PyExc_IndexError);
                return SIP_NULLPTR;
            }}

''')


def _const_cast(spec, type, value):
    """ Return a value with an appropriate const_cast to a type. """

    if type.is_const:
        cpp_type = fmt_argument_as_cpp_type(spec, type, plain=True,
                no_derefs=True)

        return f'const_cast<{cpp_type} *>({value})'

    return value


def _unique_class_ctors(spec, klass):
    """ An iterator over non-private ctors that have a unique C++ signature.
    """

    for ctor in klass.ctors:
        if ctor.access_specifier is AccessSpecifier.PRIVATE:
            continue

        if ctor.cpp_signature is None:
            continue

        for do_ctor in klass.ctors:
            if do_ctor is ctor:
                yield ctor
                break

            if do_ctor.cpp_signature is not None and same_signature(spec, do_ctor.cpp_signature, ctor.cpp_signature):
                break


def _unique_class_virtual_overloads(spec, klass):
    """ An iterator over non-private virtual overloads that have a unique C++
    signature.
    """

    for virtual_overload in klass.virtual_overloads:
        overload = virtual_overload.overload

        if overload.access_specifier is AccessSpecifier.PRIVATE:
            continue

        for do_virtual_overload in klass.virtual_overloads:
            if do_virtual_overload is virtual_overload:
                yield virtual_overload
                break

            do_overload = do_virtual_overload.overload

            if do_overload.cpp_name == overload.cpp_name and same_signature(spec, do_overload.cpp_signature, overload.cpp_signature):
                break


# TODO All uses of this should be by legacy ABIs so remove it when the backend
# refactoring is complete.
def _optional_ptr(is_ptr, name):
    """ Return an appropriate reference to an optional pointer. """

    return name if is_ptr else 'SIP_NULLPTR'


# The map of slots to C++ names.
_SLOT_NAME_MAP = {
    PySlot.ADD:         'operator+',
    PySlot.SUB:         'operator-',
    PySlot.MUL:         'operator*',
    PySlot.TRUEDIV:     'operator/',
    PySlot.MOD:         'operator%',
    PySlot.AND:         'operator&',
    PySlot.OR:          'operator|',
    PySlot.XOR:         'operator^',
    PySlot.LSHIFT:      'operator<<',
    PySlot.RSHIFT:      'operator>>',
    PySlot.IADD:        'operator+=',
    PySlot.ISUB:        'operator-=',
    PySlot.IMUL:        'operator*=',
    PySlot.ITRUEDIV:    'operator/=',
    PySlot.IMOD:        'operator%=',
    PySlot.IAND:        'operator&=',
    PySlot.IOR:         'operator|=',
    PySlot.IXOR:        'operator^=',
    PySlot.ILSHIFT:     'operator<<=',
    PySlot.IRSHIFT:     'operator>>=',
    PySlot.INVERT:      'operator~',
    PySlot.CALL:        'operator()',
    PySlot.GETITEM:     'operator[]',
    PySlot.LT:          'operator<',
    PySlot.LE:          'operator<=',
    PySlot.EQ:          'operator==',
    PySlot.NE:          'operator!=',
    PySlot.GT:          'operator>',
    PySlot.GE:          'operator>=',
}

def _overload_cpp_name(overload):
    """ Return the C++ name of an overload. """

    py_slot = overload.common.py_slot

    return overload.cpp_name if py_slot is None else _SLOT_NAME_MAP[py_slot]


class SourceFile:
    """ The encapsulation of a source file. """

    def __init__(self, source_name, description, module, project, generated):
        """ Initialise the object. """

        self._description = description
        self._module = module

        self.open(source_name, project)

        generated.append(source_name)

    def __enter__(self):
        """ Implement a context manager for the file. """

        return self

    def __exit__(self, exc_type, exc_value, traceback):
        """ Implement a context manager for the file. """

        self.close()

    def close(self):
        """ Close the source file. """

        self._f.close()

    def open(self, source_name, project):
        """ Open a source file and make it current. """

        self._f = open(source_name, 'w', encoding='UTF-8')

        self._line_nr = 1

        self._write_header_comments(self._description, self._module,
                project.version_info)

    def write(self, s):
        """ Write a string while tracking the current line number. """

        # Older C++ standards (pre-C++17) get confused with digraphs (usually
        # when the default setuptools is being used to build C++ extensions).
        # The easiest solution is to hack the string for the most common case
        # and hope it doesn't have unintended consequences.
        self._f.write(s.replace('_cast<::', '_cast< ::'))
        self._line_nr += s.count('\n')

    def write_code(self, code):
        """ Write some handwritten code. """

        # A trivial case.
        if code is None:
            return

        # The code may be a single code block or a list of them.
        code_blocks = code if isinstance(code, list) else [code]

        # Another trivial case.
        if not code_blocks:
            return

        for code_block in code_blocks:
            self.write(f'#line {code_block.line_nr} "{self._posix_path(code_block.sip_file)}"\n')
            self.write(code_block.text)

        self.write(f'#line {self._line_nr + 1} "{self._posix_path(self._f.name)}"\n')

    @staticmethod
    def _posix_path(path):
        """ Return the POSIX format of a path. """

        return path.replace('\\', '/')

    def _write_header_comments(self, description, module, version_info):
        """ Write the comments at the start of the file. """

        version_info_s = f' *\n * Generated by SIP {SIP_VERSION_STR}\n' if version_info else ''

        copying_s = fmt_copying(module.copying, ' *')

        self.write(
f'''/*
 * {description}
{version_info_s}{copying_s} */

''')


class CompilationUnit(SourceFile):
    """ Encapsulate a compilation unit, ie. a C or C++ source file. """

    def __init__(self, source_name, description, module, project, buildable,
            sip_api_file=True):
        """ Initialise the object. """

        super().__init__(source_name, description, module, project,
                buildable.sources)

        self.write_code(module.unit_code)

        if sip_api_file:
            self.write(f'#include "sipAPI{module.py_name}.h"\n')
