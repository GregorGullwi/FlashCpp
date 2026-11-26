// [dcl.init.ref]/5.4.4: when a conversion result referent is reference-related
// to an rvalue reference target, the result must not be an lvalue. operator
// int&() therefore cannot initialize int&& through a materialized temporary.
struct A {
	operator int&();
};

int&& r = A();
