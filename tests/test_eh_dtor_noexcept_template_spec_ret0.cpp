// Test: noexcept(false) destructor on a full template specialization is correctly
// propagated through the instantiate_full_specialization() path.
// This exercises Parser_Templates_Inst_Substitution.cpp where a new
// DestructorDeclarationNode is created for the specialization — the noexcept
// flag and expression must be copied from the original.

template<typename T>
struct Wrapper {
	T value;
	~Wrapper() {}  // implicitly noexcept(true)
};

// Full specialization with noexcept(false) destructor
template<>
struct Wrapper<int> {
	int value;
	~Wrapper() noexcept(false) {}  // explicitly may throw
};

// Full specialization with default (implicit noexcept(true)) destructor
template<>
struct Wrapper<double> {
	double value;
	~Wrapper() {}  // implicitly noexcept(true)
};

// Full specialization with explicit noexcept(true) destructor
template<>
struct Wrapper<char> {
	char value;
	~Wrapper() noexcept {}  // explicitly noexcept(true)
};

int main() {
	int result = 0;

	// Wrapper<int> has noexcept(false) — should NOT be nothrow destructible
	if (__is_nothrow_destructible(Wrapper<int>))
		result |= 1;

	// Wrapper<double> has implicit noexcept(true) — should be nothrow destructible
	if (!__is_nothrow_destructible(Wrapper<double>))
		result |= 2;

	// Wrapper<char> has explicit noexcept(true) — should be nothrow destructible
	if (!__is_nothrow_destructible(Wrapper<char>))
		result |= 4;

	// Wrapper<float> uses the primary template — should be nothrow destructible
	if (!__is_nothrow_destructible(Wrapper<float>))
		result |= 8;

	// Also verify via the noexcept operator on pseudo-destructor calls
	Wrapper<int> wi{42};
	if (noexcept(wi.~Wrapper()))
		result |= 16;

	Wrapper<double> wd{3.14};
	if (!noexcept(wd.~Wrapper()))
		result |= 32;

	return result;
}
