template <typename T>
struct EmptyView {
};

namespace views {
	template <typename T>
	constexpr EmptyView<T> empty;

	template <typename T>
	constexpr EmptyView<T*> empty<T*>;

	template <typename T>
	concept PointerLike = true;

	template <typename T>
		requires PointerLike<T>
	constexpr EmptyView<const T*> constrained_empty<const T*>;
}

int main() {
	(void) views::empty<int>;
	return 0;
}
