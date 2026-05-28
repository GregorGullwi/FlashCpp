// Regression test: extern declarations without initializers must not
// allocate local storage (C++20 [basic.link]).
//
// Before the fix, FlashCpp emitted a strong definition for the extern
// declaration, causing duplicate-definition errors at link time.
//
// After the fix, the extern declaration is emitted as an undefined
// external reference, resolved by the definition in the helper .c file.

extern int testExternVar;

int main() {
	return testExternVar;
}
