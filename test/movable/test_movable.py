# SPDX-License-Identifier: BSD-2-Clause

# Copyright (c) 2025 Phil Thompson <phil@riverbankcomputing.com>


from utils import SIPTestCase


class MovableTestCase(SIPTestCase):
    """ Test the support for the /Movable/ mapped type annotation.  (See
    issue/60.)
    """

    def test_Movable(self):
        """ Test the support for /Movable/.  It also verifies the test
        implementation of the std::unique_ptr mapped type.
        """

        from sys import getrefcount
        from movable_module import AnObject, ObjectWrapper

        ao = AnObject(3)
        ow = ObjectWrapper()

        # As of Python v3.14 we can't make assumptions about initial
        # reference counts so we test for increases and decreases rather than
        # absolute values.
        ao_base_refcount = getrefcount(ao)

        # Test the value of the object.
        self.assertEqual(ao.getValue(), 3)
        self.assertEqual(getrefcount(ao), ao_base_refcount)

        # Test an empty wrapper.
        self.assertEqual(ow.getObjectValue(), -1000)

        # Test an non-empty wrapper.
        ow.setObject(ao)
        self.assertEqual(getrefcount(ao), ao_base_refcount + 1)
        self.assertEqual(ow.getObjectValue(), 4)

        # Unwrap the object and test the wrapper.
        ao2 = ow.takeObject()
        self.assertEqual(ow.getObjectValue(), -1000)

        # Re-test the value of the object.
        self.assertEqual(ao2.getValue(), 4)

        # Check that the original Python object no longer wraps the C++ object.
        self.assertEqual(getrefcount(ao), ao_base_refcount)
        self.assertRaises(RuntimeError, ao.getValue)
