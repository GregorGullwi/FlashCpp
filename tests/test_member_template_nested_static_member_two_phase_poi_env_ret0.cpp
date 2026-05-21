// Regression: nested member-template static member replay must preserve both
// outer and inner template environments while completing dependent lookup at POI.

constexpr int adl_nested(long) {
	return 1;
}

template <typename T, int OuterN>
struct Outer {
	template <typename U>
	struct Inner {
		static constexpr int value =
			adl_nested(T{}) + OuterN + static_cast<int>(sizeof(U));
	};
};

namespace N {
	struct Tag {
		friend constexpr int adl_nested(Tag) {
			return 36;
		}
	};
}

int main() {
	using Inst = typename Outer<N::Tag, 2>::template Inner<int>;
	return Inst::value == 42 ? 0 : 1;
}
