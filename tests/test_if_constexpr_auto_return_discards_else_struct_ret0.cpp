// Regression: auto return deduction must ignore discarded if constexpr
// branches. C++17 [stmt.if]/4: the discarded substatement is not instantiated,
// so a taken branch returning a class type must not be mixed with a discarded
// branch that returns a different type.
enum class Strategy {
	UseIter,
	UseOther
};

struct Iter {
	int* p;
};

struct WideIter {
	long long* p;
};

struct Other {
	int value;
};

template <class T>
constexpr Strategy choose() {
	return Strategy::UseIter;
}

template <class Range>
auto pick_begin(Range& range) {
	constexpr Strategy selected = choose<Range>();
	if constexpr (selected == Strategy::UseIter) {
		return range.begin();
	} else {
		return Other{0};
	}
}

struct IntRange {
	int* first;
	Iter begin() {
		return Iter{first};
	}
};

struct WideRange {
	long long* first;
	WideIter begin() {
		return WideIter{first};
	}
};

int main() {
	int small[2] = {3, 4};
	IntRange ints{small};
	Iter it = pick_begin(ints);
	if (it.p != small) {
		return 1;
	}

	long long wide[2] = {11, 13};
	WideRange wides{wide};
	WideIter wit = pick_begin(wides);
	return wit.p == wide ? 0 : 2;
}
