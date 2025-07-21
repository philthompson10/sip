// The SIP implementation of the callables test module.


%Module(name=callables_module)


%ModuleHeaderCode
bool module_procedure_called;

void module_void_ret_no_args()
{
    module_procedure_called = true;
}
%End


bool module_procedure_called;

void module_void_ret_no_args();
