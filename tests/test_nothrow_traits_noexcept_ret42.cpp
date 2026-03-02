// Test that __is_nothrow_constructible and __is_nothrow_assignable
// correctly detect noexcept on user-defined special members.
struct Foo {
	Foo() noexcept {}
	Foo& operator=(const Foo&) noexcept { return *this; }
};

struct Bar {
	Bar() {}  // not noexcept
	Bar& operator=(const Bar&) { return *this; }  // not noexcept
};

int main() {
	int result = 0;
	// Foo has noexcept ctor and assignment
	if (__is_nothrow_constructible(Foo))
		result += 10;
	if (__is_nothrow_assignable(Foo&, const Foo&))
		result += 10;
	// Bar does NOT have noexcept ctor or assignment
	if (!__is_nothrow_constructible(Bar))
		result += 11;
	if (!__is_nothrow_assignable(Bar&, const Bar&))
		result += 11;
	return result;  // expect 42
}
