// [dcl.init.ref]/5.4.3 (CWG 1571): indirect reference binding through a
// conversion function may not drop the result referent's volatile
// qualification. The direct-binding plan rejects operator volatile int&() for
// const int&, and the value fallback must not accept it either.
struct A {
	operator volatile int&();
};

const int& r = A();
