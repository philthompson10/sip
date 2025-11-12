// The SIP implementation of the class_misc_module test module.


%Module(name=class_misc_module)


%ModuleHeaderCode
class Klass
{
public:
    class Nested
    {
    };
};

class SimpleKlass
{
};
%End


class Klass
{
public:
    class Nested
    {
    };
};

class SimpleKlass /Supertype=class_misc_module.simplewrapper/
{
};
