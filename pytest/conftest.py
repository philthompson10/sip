# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


import glob
import importlib
import inspect
import os
import subprocess
import sys

import pytest


# The different configurations the tests are run for.
CONFIGURATIONS = ('12', '13')


@pytest.fixture(scope='module', params=CONFIGURATIONS)
def module(request):
    """ The fixture is an appropriately built and imported wrapped modules. """

    # See if we want to build a separate sip module.  We will do anyway if
    # there is more than one wrapped module to build
    use_sip_module = getattr(request.module, 'use_sip_module', False)

    # Get the directory containing the particular test.
    test_dir = os.path.dirname(inspect.getfile(request.module))

    # It is assumed that there will be one .sip for each module to be built.
    sip_files = glob.glob(os.path.join(test_dir, '*.sip'))

    # TODO This fixture should only handle one wrapped module.
    assert len(sip_files) == 1

    if len(sip_file) > 1:
        use_sip_module = True

    # Build each module.
    module_names = []
    for sip_file in sip_files:
        module_names.append(
                _build_test_module(sip_file, test_dir,
                        use_sip_module=use_sip_module)

    # Build a sip module if required.
    if use_sip_module:
        module_names.append(_build_sip_module(test_dir))

    # Fix the path.
    # TODO Is this needed?
    sys.path.insert(0, test_dir)

    importlib.import_module(module_names[0])

    yield sys.modules[module_names[0]]

    # Unload each module.
    for module_name in module_names:
        try:
            del sys.modules[module_name]
        except KeyError:
            pass

    # Restore the path.
    del sys.path[0]


# The prototype pyproject.toml file.
_PYPROJECT_TOML = """
[build-system]
requires = ["sip >=6"]
build-backend = "sipbuild.api"

[project]
name = "{module_name}"

[tool.sip.project]
minimum-macos-version = "10.9"
sip-files-dir = ".."
"""

_ABI_VERSION = """
abi-version = "{abi_version}"
"""

_SIP_MODULE = """
sip-module = "{sip_module}"
"""


def _build_module(module_name, build_args, src_dir, test_dir,
        impl_subdirs=None, no_compile=False):
    """ Build a module and move any implementation to the test directory. """

    cwd = os.getcwd()
    os.chdir(src_dir)

    ns_dir = test_dir

    # Do the build.
    args = [sys.executable] + build_args

    if no_compile:
        args.append('--no-compile')

    subprocess.run(args).check_returncode()

    if no_compile:
        return

    # Find the implementation.  Note that we only support setuptools and
        # not distutils.
    impl_pattern = ['build']

    if impl_subdirs is not None:
        impl_pattern.extend(impl_subdirs)

    impl_pattern.append('lib*')

    #if cls.namespace is not None:
    #    ns_subdirs = cls.namespace.split('.')
    #    impl_pattern.extend(ns_subdirs)

    #    ns_dir = os.path.join(ns_dir, os.path.join(*ns_subdirs))
    #    os.makedirs(ns_dir, exist_ok=True)

    impl_pattern.append(
            module_name + '*.pyd' if sys.platform == 'win32' else '*.so')

    impl_paths = glob.glob(os.path.join(*impl_pattern))
    if len(impl_paths) == 0:
        raise Exception(
                "no '{0}' extension module was built".format(module_name))

    if len(impl_paths) != 1:
        raise Exception(
                "unable to determine file name of the '{0}' extension module".format(module_name))

    impl_path = os.path.abspath(impl_paths[0])
    impl = os.path.basename(impl_path)

    os.chdir(ns_dir)

    try:
        os.remove(impl)
    except:
        pass

    os.rename(impl_path, impl)

    os.chdir(cwd)


def _build_test_module(sip_file, test_dir, use_sip_module=False,
        no_compile=False):
    """ Build a test extension module and return its name. """

    build_dir = sip_file[:-4]
    module_name = os.path.basename(build_dir)

    # Remove any previous build directory.
    shutil.rmtree(build_dir, ignore_errors=True)

    os.mkdir(build_dir)

    # Create a pyproject.toml file.
    pyproject_toml = os.path.join(build_dir, 'pyproject.toml')

    with open(pyproject_toml, 'w') as f:
        f.write(_PYPROJECT_TOML.format(module_name=module_name))

        #if cls.abi_version is not None:
        #    f.write(_ABI_VERSION.format(abi_version=cls.abi_version))

        #if use_sip_module:
        #    sip_module = 'sip' if cls.namespace is None else cls.namespace + '.sip'
        #    f.write(_SIP_MODULE.format(sip_module=sip_module))

        #if cls.tags is not None or cls.exceptions:
        #    f.write(f'\n[tool.sip.bindings.{module_name}]\n')

        #    if cls.tags is not None:
        #        tags_s = ', '.join([f'"{t}"' for t in cls.tags])
        #        f.write(f'tags = [{tags_s}]\n')

        #    if cls.exceptions:
        #        f.write('exceptions = true\n')

    # Build and move the test module.
    _build_module(module_name, ['-m', 'sipbuild.tools.build', '--verbose'],
            build_dir, test_dir, impl_subdirs=[module_name, 'build'],
            no_compile=no_compile)

    return module_name


def _build_sip_module(test_dir):
    """ Build the sip module and return its name. """

    sip_module_name = 'sip'

    #if cls.namespace is not None:
    #    sip_module_name = f'{cls.namespace}.{sip_module_name}'

    # TODO Take the ABI major version into account.
    sdist_glob = os.path.join(root_dir,
            sip_module_name.replace('.', '_') + '-*.tar.gz')

    # Remove any existing sdists.
    for old in glob.glob(sdist_glob):
        os.remove(old)

    # Create the sdist.
    args = [sys.executable, '-m', 'sipbuild.tools.module', '--sdist',
        '--target-dir', root_dir
    ]

    if cls.abi_version is not None:
        args.append('--abi-version')
        args.append(cls.abi_version)

    #if cls.sip_module_configuration is not None:
    #    for option in cls.sip_module_configuration:
    #        args.append('--option')
    #        args.append(option)

    args.append(sip_module_name)

    subprocess.run(args).check_returncode()

    # Find the sdist and unpack it.
    sdists = glob.glob(sdist_glob)

    if len(sdists) != 1:
        raise Exception(
            "unable to determine the name of the sip module sdist file")

    sdist = sdists[0]

    with tarfile.open(sdist) as tf:
        tf.extractall(path=root_dir, filter='data')

    # Build and move the module.
    src_dir = os.path.join(root_dir, os.path.basename(sdist)[:-7])

    _build_module(sip_module_name, ['setup.py', 'build'], src_dir, test_dir)

    return sip_module_name
