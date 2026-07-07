template <typename T>
struct EmptyView {
};

namespace views {
	template <typename T>
	constexpr EmptyView<T> empty;
}

int main() {
	(void) views::empty<int>;
	return 0;
}
