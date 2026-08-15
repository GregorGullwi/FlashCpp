// Regression: instantiating a class-template member whose noexcept depends on
// a nested `::value` trait must not reject the specification as non-constant.

template <class T>
struct IsNothrowSwappable {
	static constexpr bool value = sizeof(T) > 1;
};

template <class Compare>
struct Tree {
	void swap(Tree&) noexcept(IsNothrowSwappable<Compare>::value) {}
};

struct Tiny {
	char c;
};

struct Wide {
	int a;
	int b;
};

int main() {
	Tree<Tiny> tiny_a{};
	Tree<Tiny> tiny_b{};
	Tree<Wide> wide_a{};
	Tree<Wide> wide_b{};
	tiny_a.swap(tiny_b);
	wide_a.swap(wide_b);
	return 0;
}
