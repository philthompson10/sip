// The SIP implementation of the classes_module test module.


%Module(name=classes_module)


%ModuleHeaderCode
class Klass
{
public:
    class Nested
    {
    };

    Klass() : attr(0) {}

    static int s_attr;
    int attr;
};

class SimpleKlass
{
};
%End


%ModuleCode
int Klass::s_attr = 0;
%End


class Klass
{
public:
    class Nested
    {
    };

    Klass();

    static int s_attr;
    int attr;
};

class SimpleKlass /Supertype=classes_module.simplewrapper/
{
};
