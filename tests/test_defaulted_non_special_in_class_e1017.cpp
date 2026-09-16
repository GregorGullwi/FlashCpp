// Only special member functions and comparison operators may be defaulted
// ([dcl.fct.def.default]/1); an ordinary in-class member function may not.
struct Host {
	void f() = default;
};
