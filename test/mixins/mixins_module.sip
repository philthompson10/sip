// The SIP implementation of the mixins_module test module.


%Module(name=mixins_module, call_super_init=True)


%ModuleHeaderCode
class Mixin
{
public:
    Mixin(int mixin_value=0) : mixin_attr(mixin_value) {}

    int get_mixin_attr() const {return mixin_attr;}
    void set_mixin_attr(int value) {mixin_attr = value;}
    int mixin_attr;
};

class Klass
{
public:
    Klass(int value=0) : attr(value) {}

    int get_attr() const {return attr;}
    void set_attr(int value) {attr = value;}
    int attr;
};
%End


class Mixin
{
public:
    Mixin(int mixin_value=0);

    int get_mixin_attr() const;
    void set_mixin_attr(int value);
    int mixin_attr;
};

class Klass
{
public:
    Klass(int value=0);

    int get_attr() const;
    void set_attr(int value);
    int attr;
};
