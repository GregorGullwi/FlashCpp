constexpr int adl_pick(long) {
	return 1;
}

template <typename U>
struct Holder {
	template <typename T>
	struct Box;

	template <typename T>
	struct Box<T*> {
		static constexpr int value = adl_pick(T{});
	};
};

namespace N {
	struct Tag {
		friend constexpr int adl_pick(Tag) {
			return 42;
		}
	};
}

int main() {
	using Inst = typename Holder<int>::template Box<N::Tag*>;
	return Inst::value == 42 ? 0 : 1;
}
