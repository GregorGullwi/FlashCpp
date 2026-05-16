int f(long) {
	return 7;
}

namespace N {
	struct Tag {
		friend constexpr int f(Tag) {
			return 42;
		}
	};
}

template <typename T>
struct Box {
	static constexpr int value = f(T{});
};

int main() {
	if (Box<N::Tag>::value != 42) {
		return 1;
	}
	return 0;
}
