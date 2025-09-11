// The SIP implementation of the namespaces_module test module.


%Module(name=namespaces_module)


%ModuleHeaderCode
namespace NS
{
    int attr;
    int get_attr() {return attr;}
};
%End


%ModuleCode
int NS::attr = 0;
%End


namespace NS
{
    int attr;
    int get_attr();
};
