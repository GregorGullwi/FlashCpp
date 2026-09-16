// A deleted definition must be the first declaration ([dcl.fct.def.delete]/1);
// an out-of-line member definition is not.
struct Host {
	void f();
};

void Host::f() = delete;
