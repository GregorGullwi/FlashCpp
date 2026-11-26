// [dcl.init.ref]/5.4.3: the inverse CWG 1571 shape. operator const int&() may
// bind refs that keep or add const, but not a volatile int& that drops const.
struct A {
	operator const int&();
};

volatile int& r = A();
