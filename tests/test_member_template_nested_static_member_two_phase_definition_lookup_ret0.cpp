// Regression: nested member-template static member definitions must preserve
// definition-time ordinary lookup while replay still carries outer + inner bindings.

int choose_nested(long) {
	return 1;
}

template <typename T, int OuterN>
struct Outer {
	template <int InnerN>
	struct Inner {
		static constexpr int value =
			choose_nested(0) + static_cast<int>(sizeof(T)) + OuterN + InnerN;
	};
};

int choose_nested(int) {
	return 100;
}

int main() {
	using Inst = typename Outer<long long, 3>::template Inner<5>;
	return Inst::value - 17;
}
