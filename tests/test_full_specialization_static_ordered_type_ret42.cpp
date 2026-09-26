template <typename T>
struct FullOrderedHolder;

template <>
struct FullOrderedHolder<int> {
	static inline int (*(*value)[3])[4] = nullptr;
};

int main() {
	FullOrderedHolder<int> object;
	return FullOrderedHolder<int>::value == nullptr &&
		object.value == nullptr
		? 42
		: 0;
}
