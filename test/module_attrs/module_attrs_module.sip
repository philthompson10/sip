// The SIP implementation of the module_attrs test module.


%Module(name=module_attrs_module)


%ModuleHeaderCode
// POD types.
int int_attr = 10;

// Read only attributes.
const int int_attr_const = 10;
int int_attr_no_setter = 10;

// /PyName/ annotation.
int int_attr_py_name = 10;

// %GetCode.
int int_attr_getter = 10;

// %SetCode.
int int_attr_setter = 10;

// %SetCode that raises an exception.
int int_attr_bad_setter = 10;
%End


// POD types.
int int_attr;

// Read only attributes.
const int int_attr_const;
int int_attr_no_setter /NoSetter/;

// /PyName/ annotation.
int int_attr_py_name /PyName=py_int_attr/;

// %GetCode.
int int_attr_getter {
%GetCode
    sipPy = PyLong_FromLong(2 * int_attr_getter);
%End
};

// %SetCode.
int int_attr_setter {
%SetCode
    int_attr_setter = 2 * (int)PyLong_AsLong(sipPy);
%End
};

// %SetCode that raises an exception.
int int_attr_bad_setter {
%SetCode
    PyErr_SetNone(PyExc_NameError);
    sipErr = 1;
%End
};
