// The SIP implementation of the class_callables_module test module.


%Module(name=class_callables_module)


%ModuleHeaderCode
class Klass
{
public:
    Klass() : attr_int(0) {}

    static int get_s_attr_int() {return s_attr_int;}
    static void set_s_attr_int(int value) {s_attr_int = value;}

    int get_attr_int() const {return attr_int;}
    void set_attr_int(int value) {attr_int = value;}

    int operator-() {return -attr_int;}
    bool operator==(const Klass &other) {return attr_int == other.attr_int;}
    int operator()(int multiplier) {return attr_int * multiplier;}

    int count() const {return 5;}
    int operator[](int idx) const {return idx;}

private:
    static int s_attr_int;
    int attr_int;
};
%End


%ModuleCode
int Klass::s_attr_int = 0;
%End


class Klass
{
public:
    Klass();

    static int get_s_attr_int();
    static void set_s_attr_int(int value);

    int get_attr_int() const;
    void set_attr_int(int value);

    int operator-();
    bool operator==(const Klass &other);
    int operator()(int multiplier);

    int count() const /__len__/;
    int operator[](int idx) const;
};
