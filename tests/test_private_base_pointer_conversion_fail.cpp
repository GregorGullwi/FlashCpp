// Test that implicit Derived* -> PrivateBase* conversion is rejected.
// Per C++20 [conv.ptr]/3, the base class must be accessible.
//
// TODO: This test should be a _fail test, but FlashCpp does not yet enforce
// access control on pointer variable initialization (only on overload
// resolution). The isTransitivelyDerivedFrom fix in OverloadResolution.h
// correctly filters private bases for function argument matching, but the
// IrGenerator_Stmt_Decl.cpp pointer init path does not call it.
// Once access control is enforced on pointer init, rename this file to
// test_private_base_pointer_conversion_fail.cpp.

struct Base {
	int x;
};

struct Derived : private Base {
	int y;
};

int main() {
	Derived d;
	// This SHOULD fail but currently compiles — see TODO above.
	Base* bp = &d;
	return bp->x;
}
