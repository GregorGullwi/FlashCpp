// Deep inheritance walks must use worklists, not native recursion. The nested
// enum at the deepest base forces member-context lookup through the whole chain.

template<int N> struct DeepHierarchy;
template<int N> struct DeepHierarchy : DeepHierarchy<N - 1> {};

template<> struct DeepHierarchy<0> {
	enum class Tag {
		value = 7,
	};
};

struct DeepestLookup : DeepHierarchy<1024> {
	int lookup() {
		return static_cast<int>(Tag::value);
	}
};

int main() {
	DeepestLookup lookup;
	return lookup.lookup() == 7 ? 0 : 1;
}
