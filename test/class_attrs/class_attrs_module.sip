// The SIP implementation of the class_attrs_module test module.


%Module(name=class_attrs_module)


%ModuleHeaderCode
class Klass
{
public:
    class Nested
    {
    };

    Klass() : attr(0) {}

    static int s_attr;
    static int get_s_attr() {return s_attr;}

    int attr;
    int get_attr() {return attr;}
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
    static int get_s_attr();

    int attr;
    int get_attr();
};

class SimpleKlass /Supertype=class_attrs_module.simplewrapper/
{
};
