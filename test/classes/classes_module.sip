// The SIP implementation of the classes_module test module.


%Module(name=classes_module)


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

class SimpleKlass /Supertype=classes_module.simplewrapper/
{
};
