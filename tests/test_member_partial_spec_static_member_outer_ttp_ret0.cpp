// Regression: member-template partial-specialization static member replay must
// preserve enclosing template-template parameter bindings.
template <typename T>
struct Wrap {
	static constexpr int value = sizeof(T);
};

template <template <typename> class Meta>
struct Outer {
	template <typename...>
	struct Inner {
		static constexpr int value = 0;
	};

	template <typename T>
	struct Inner<T> {
		static constexpr int value = Meta<T>::value;
	};
};

int main() {
	Outer<Wrap>::Inner<int> inner;
	int ok[(decltype(inner)::value == sizeof(int)) ? 1 : -1];
	return sizeof(ok) == sizeof(int) ? 0 : 1;
}
