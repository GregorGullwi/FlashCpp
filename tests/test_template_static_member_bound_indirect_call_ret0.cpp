// Regression: a structurally substituted static member initializer whose call
// target is a bound indirect call must not be pushed back to source replay just
// because the call has no direct FunctionDeclarationNode. The qualified owner
// also resolves structurally, so the whole initializer is accepted.
//
// The indirect call sits in the unchosen arm so the test observes the accepted
// structural result without depending on constexpr evaluation of the pointer.

constexpr int forty_two() { return 42; }
constexpr int (*pfn)() = &forty_two;

struct Provider {
	static constexpr int zero = 0;
};

template <typename T>
struct Holder {
	static constexpr int value = T::zero + (false ? pfn() : 0);
};

int main() {
	return Holder<Provider>::value == 0 ? 0 : 1;
}
