// An out-of-line '= default' on an ordinary member function is ill-formed.
struct Host {
	void f();
};

void Host::f() = default;
