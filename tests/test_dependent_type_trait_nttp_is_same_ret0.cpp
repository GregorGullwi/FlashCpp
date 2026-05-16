template <bool B>
struct Flag {
	static constexpr int value = B ? 1 : 0;
};

template <typename T>
struct TraitUse {
	static int first() { return Flag<__is_same(T, T)>::value; }
	static int second() { return Flag<__is_same(T, const T)>::value; }
};

int main() {
	return TraitUse<int>::first() != TraitUse<int>::second() ? 0 : 1;
}
