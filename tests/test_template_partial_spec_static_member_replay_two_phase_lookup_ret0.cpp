// Regression: static-member initializer replay in partial specializations must preserve
// definition-context lookup (friend ADL) during two-phase template instantiation.
constexpr int adl_pick(long) {
	return 1;
}

template <typename T>
struct Box;

template <typename T>
struct Box<T*> {
	static constexpr int value = adl_pick(T{});
};

namespace N {
	struct Tag {
		friend constexpr int adl_pick(Tag) {
			return 42;
		}
	};
}

int main() {
	return Box<N::Tag*>::value == 42 ? 0 : 1;
}
