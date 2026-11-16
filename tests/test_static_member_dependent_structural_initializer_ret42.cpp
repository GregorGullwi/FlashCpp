template <int N> struct DependentStructuralHolder {
	static constexpr int value = N * 2;
};

template <typename T> struct DependentSizeHolder {
	static constexpr int value = sizeof(T) + 38;
};

static_assert(DependentStructuralHolder<21>::value == 42);
static_assert(DependentSizeHolder<int>::value == 42);

int main() {
	return DependentStructuralHolder<21>::value + DependentSizeHolder<int>::value - 42;
}
