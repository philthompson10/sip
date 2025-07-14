// The SIP implementation of the module_attrs test module.


%Module(name=module_attrs_module)


%ModuleHeaderCode
int int_attr = 10;
const int int_attr_const = 50;
%End


int int_attr;
const int int_attr_const;
