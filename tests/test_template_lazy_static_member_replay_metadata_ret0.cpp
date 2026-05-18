// Regression: when an existing lazily-registered static member gets its initializer
// patched from the primary template path, replay metadata must be propagated too.
// This preserves two-phase dependent-call completion through lazy static replay.

constexpr int pick(long) {
	return 7;
}

template <typename T>
struct Box {
	static constexpr int value = pick(T{});
};

template <typename T>
struct UseBox {
	static constexpr int value = Box<T>::value;
};

namespace N {
struct Tag {
	friend constexpr int pick(Tag) {
		return 42;
	}
};
}

int main() {
	if (UseBox<N::Tag>::value != 42) {
		return 1;
	}
	return UseBox<int>::value == 7 ? 0 : 2;
}
