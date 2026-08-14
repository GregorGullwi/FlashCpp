// Concept-ids with default template arguments must bind the defaulted
// parameter when the concept is used with fewer explicit arguments.
// The defaulted parameter is used in the constraint, unlike a tautology.
// Reduced from MSVC <compare> three_way_comparable_with's defaulted _Cat.

struct PartialCat {
	static constexpr int id = 1;
};

struct OtherCat {
	static constexpr int id = 2;
};

template <class Type, class Category = PartialCat>
concept comparable_like = __is_same(Category, PartialCat);

template <class Left, class Right, class Category = PartialCat>
concept comparable_with_like =
	comparable_like<Left, Category> && comparable_like<Right, Category>;

template <class Left, class Right>
constexpr int synth_compare() {
	if constexpr (comparable_with_like<Left, Right>) {
		return 0;
	} else {
		return 1;
	}
}

template <class Left, class Right>
constexpr int synth_compare_other() {
	if constexpr (comparable_with_like<Left, Right, OtherCat>) {
		return 0;
	} else {
		return 1;
	}
}

struct Box {
	int value;
	long long extra;
};

int main() {
	static_assert(synth_compare<int, int>() == 0);
	static_assert(synth_compare<int, long long>() == 0);
	static_assert(synth_compare<Box, Box>() == 0);
	static_assert(synth_compare_other<int, int>() == 1);
	return synth_compare<Box, int>();
}
