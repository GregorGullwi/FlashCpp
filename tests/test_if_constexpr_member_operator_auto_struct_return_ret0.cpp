// Regression: member operator() auto return deduction must ignore discarded
// if constexpr branches. A 16-byte range vs 8-byte iterator makes a wrong
// deduced return visible to codegen (same-size structs can hide the mismatch).
enum class Strategy {
	None,
	Member
};

struct Iter {
	int* p;
};

struct Range {
	int* first;
	int* last;
	Iter begin() {
		return Iter{first};
	}
};

struct BeginCpo {
	template <class T>
	constexpr auto operator()(T&& value) const {
		constexpr Strategy strat = Strategy::Member;
		if constexpr (strat == Strategy::Member) {
			return value.begin();
		} else {
			return value;
		}
	}
};

inline constexpr BeginCpo begin_cpo{};

int main() {
	int xs[2] = {3, 4};
	Range r{xs, xs + 2};
	Iter it = begin_cpo(r);
	return it.p == xs ? 0 : 1;
}
