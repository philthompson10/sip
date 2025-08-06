// The SIP implementation of the classes_module test module.


%Module(name=classes_module)


%ModuleHeaderCode
class Klass
{
};

class SimpleKlass
{
};
%End


class Klass
{
};

class SimpleKlass /Supertype=classes_module.simplewrapper/
{
};
