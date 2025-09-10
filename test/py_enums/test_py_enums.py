# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from unittest import skip

from enum import Enum, Flag, IntEnum, IntFlag

from utils import SIPTestCase


@skip("Needs porting to ABI v14")
class PyEnumsTestCase(SIPTestCase):
    """ Test the support for Python enums. """

    # The sip module configuration.
    sip_module_configuration = ['PyEnums']

    @classmethod
    def setUpClass(cls):
        """ Set up the test case. """

        super().setUpClass()

        from py_enums_module import Klass

        class NamedEnumFixture(Klass):
            """ A fixture for testing named enum values. """

            def __init__(self, value):
                """ Initialise the object. """

                super().__init__()

                self._value = value

            def named_virt(self):
                return self._value

        cls.member_fixture = NamedEnumFixture(
                Klass.ClassNamedEnum.ClassNamedMember)
        cls.int_fixture = NamedEnumFixture(0)

        class ScopedEnumFixture(Klass):
            """ A fixture for testing scoped enum values. """

            def scoped_virt(self):
                return Klass.ClassScopedEnum.ClassScopedMember

        cls.scoped_enum_fixture = ScopedEnumFixture()

    @classmethod
    def tearDownClass(cls):
        """ Tear down the test case. """

        # Remove all references to the extension module so that the superclass
        # can unload it.
        del cls.member_fixture
        del cls.int_fixture

        del cls.scoped_enum_fixture

        super().tearDownClass()

    ###########################################################################
    # The following test anonymous enums.
    ###########################################################################

    def test_ModuleAnon(self):
        """ Test a module level anonymous enum. """

        from py_enums_module import AnonMember

        self.assertIsInstance(AnonMember, int)
        self.assertEqual(AnonMember, 10)

    def test_ClassAnon(self):
        """ Test a class level anonymous enum. """

        from py_enums_module import Klass

        self.assertIsInstance(Klass.ClassAnonMember, int)
        self.assertEqual(Klass.ClassAnonMember, 40)

    ###########################################################################
    # The following test the /BaseType/ annotation.
    ###########################################################################

    def test_Enum_BaseType(self):
        """ Test /BaseType=Enum/. """

        from py_enums_module import EnumBase

        self.assertTrue(issubclass(EnumBase, Enum))
        self.assertFalse(issubclass(EnumBase, Flag))
        self.assertFalse(issubclass(EnumBase, IntEnum))
        self.assertFalse(issubclass(EnumBase, IntFlag))

    def test_Flag_BaseType(self):
        """ Test /BaseType=Flag/. """

        from py_enums_module import FlagBase

        self.assertTrue(issubclass(FlagBase, Flag))
        self.assertFalse(issubclass(FlagBase, IntEnum))
        self.assertFalse(issubclass(FlagBase, IntFlag))

    def test_IntEnum_BaseType(self):
        """ Test /BaseType=IntEnum/. """

        from py_enums_module import IntEnumBase

        self.assertFalse(issubclass(IntEnumBase, Flag))
        self.assertTrue(issubclass(IntEnumBase, IntEnum))
        self.assertFalse(issubclass(IntEnumBase, IntFlag))

    def test_IntFlag_BaseType(self):
        """ Test /BaseType=IntFlag/. """

        from py_enums_module import IntFlagBase

        self.assertFalse(issubclass(IntFlagBase, IntEnum))
        self.assertTrue(issubclass(IntFlagBase, IntFlag))

    ###########################################################################
    # The following test named enums.
    ###########################################################################

    def test_ModuleNamed(self):
        """ Test a module level named enum. """

        from py_enums_module import NamedEnum

        self.assertTrue(issubclass(NamedEnum, Enum))
        self.assertEqual(NamedEnum.NamedMember.value, 20)

    def test_ClassNamed(self):
        """ Test a class level named enum. """

        from py_enums_module import Klass

        self.assertTrue(issubclass(Klass.ClassNamedEnum, Enum))
        self.assertEqual(Klass.ClassNamedEnum.ClassNamedMember.value, 50)

    def test_named_get_member(self):
        """ named enum virtual result with a member value. """

        from py_enums_module import Klass

        self.install_hook()
        self.assertEqual(self.member_fixture.named_get(),
                Klass.ClassNamedEnum.ClassNamedMember)
        self.uninstall_hook()

    def test_named_set_member(self):
        """ named enum function argument with a member value. """

        from py_enums_module import Klass

        self.member_fixture.named_set(
                Klass.ClassNamedEnum.ClassNamedMember)

    def test_named_var_member(self):
        """ named enum instance variable with a member value. """

        from py_enums_module import Klass

        self.member_fixture.named_var = Klass.ClassNamedEnum.ClassNamedMember

    def test_named_overload_set(self):
        """ overloaded named enum function argument. """

        from py_enums_module import Klass

        self.member_fixture.named_overload_set(
                Klass.ClassNamedEnum.ClassNamedMember)
        self.assertTrue(self.member_fixture.named_overload)

    def test_named_get_int(self):
        """ named enum virtual result with an integer value. """

        from py_enums_module import Klass

        with self.assertRaises(TypeError):
            self.install_hook()
            self.int_fixture.named_get()
            self.uninstall_hook()

    def test_named_set_int(self):
        """ named enum function argument with an integer value. """

        with self.assertRaises(TypeError):
            self.int_fixture.named_set(50)

    def test_named_var_int(self):
        """ named enum instance variable with an integer value. """

        with self.assertRaises(TypeError):
            self.int_fixture.named_var = 50

    ###########################################################################
    # The following test scoped enums.
    ###########################################################################

    def test_ModuleScoped(self):
        """ Test a module level C++11 scoped enum. """

        from py_enums_module import ScopedEnum

        self.assertTrue(issubclass(ScopedEnum, Enum))
        self.assertEqual(ScopedEnum.ScopedMember.value, 30)

    def test_ClassScoped(self):
        """ Test a class level C++11 scoped enum. """

        from py_enums_module import Klass

        self.assertTrue(issubclass(Klass.ClassScopedEnum, Enum))
        self.assertEqual(Klass.ClassScopedEnum.ClassScopedMember.value, 70)

    def test_scoped_get_member(self):
        """ scoped enum virtual result with a member value. """

        from py_enums_module import Klass

        self.install_hook()
        self.assertIs(self.scoped_enum_fixture.scoped_get(),
                Klass.ClassScopedEnum.ClassScopedMember)
        self.uninstall_hook()

    def test_scoped_set_member(self):
        """ scoped enum function argument with a member value. """

        from py_enums_module import Klass

        self.scoped_enum_fixture.scoped_set(
                Klass.ClassScopedEnum.ClassScopedMember)

    def test_scoped_var_member(self):
        """ scoped enum instance variable with a member value. """

        from py_enums_module import Klass

        self.scoped_enum_fixture.scoped_var = Klass.ClassScopedEnum.ClassScopedMember
