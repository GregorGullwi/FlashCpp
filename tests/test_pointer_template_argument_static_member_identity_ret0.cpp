// C++20 [temp.arg.nontype]: a pointer non-type template argument identifies the
// object it points to, and a class static data member is such an object. Two static
// members with the same simple name in different classes must therefore instantiate
// different specializations, and the same member must keep its identity.
//
// The template-instance form is recorded in KNOWN_ISSUES: an instantiated class has
// no declaration-builder entity to own its member's identity yet.
struct HolderA {
	static constexpr int value = 3;
};

struct HolderB {
	static constexpr int value = 4;
};

template<class A, class B> struct Same { static constexpr bool value = false; };
template<class A> struct Same<A, A> { static constexpr bool value = true; };

template<int* Pointer> struct Tag { int marker; };

int main() {
	int result = 0;
	if (!Same<Tag<&HolderA::value>, Tag<&HolderA::value>>::value) {
		result |= 1;
	}
	if (Same<Tag<&HolderA::value>, Tag<&HolderB::value>>::value) {
		result |= 2;
	}
	return result;
}
