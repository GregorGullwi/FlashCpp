// Test: __is_nothrow_destructible correctly evaluates noexcept(false) destructors.
// This verifies the type trait (not the noexcept operator) checks the actual
// destructor's evaluated noexcept status, including noexcept(expr).

struct Throwing {
	int value;
	~Throwing() noexcept(false) {}  // explicitly may throw
};

struct Normal {
	int value;
	~Normal() {}	 // implicitly noexcept(true)
};

struct ExplicitNoexcept {
	int value;
	~ExplicitNoexcept() noexcept {}	// explicitly noexcept(true)
};

struct DefaultDtor {
	int value;
 // No user-defined destructor — implicitly noexcept(true)
};

int main() {
	int result = 0;

 // Normal should be nothrow destructible
	if (!__is_nothrow_destructible(Normal))
		result |= 1;

 // Throwing should NOT be nothrow destructible
	if (__is_nothrow_destructible(Throwing))
		result |= 2;

 // ExplicitNoexcept should be nothrow destructible
	if (!__is_nothrow_destructible(ExplicitNoexcept))
		result |= 4;

 // DefaultDtor should be nothrow destructible
	if (!__is_nothrow_destructible(DefaultDtor))
		result |= 8;

 // Scalar types should be nothrow destructible
	if (!__is_nothrow_destructible(int))
		result |= 16;

	return result;
}
