constexpr int structural_base = 40;

template <typename T> struct StructuralHolder {
	static constexpr int value = structural_base + 2;
};

template <typename T> struct StructuralAliasHolder {
	using Int = int;
	static constexpr int value = sizeof(Int) + 38;
};

static_assert(StructuralHolder<int>::value == 42);
static_assert(StructuralAliasHolder<int>::value == 42);

int main() {
	return StructuralHolder<int>::value + StructuralAliasHolder<int>::value - 42;
}
