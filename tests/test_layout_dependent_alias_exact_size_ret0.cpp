template <typename T>
struct AliasProvider {
	using value_type = T;
	using row_type = T[3];
	using grid_type = T[2][3];
};

template <typename T>
int verifyDependentAliasLayout() {
	using Value = typename AliasProvider<T>::value_type;
	using Row = typename AliasProvider<T>::row_type;
	using Grid = typename AliasProvider<T>::grid_type;

	Grid values{};
	int result = 0;
	if (sizeof(Value) != sizeof(T)) result |= 1;
	if (sizeof(Row) != sizeof(T) * 3) result |= 2;
	if (sizeof(Grid) != sizeof(T) * 6) result |= 4;
	if (sizeof(values[0]) != sizeof(T) * 3) result |= 8;
	if (sizeof(values[0][0]) != sizeof(T)) result |= 16;
	return result;
}

struct Mixed {
	long long wide;
	char narrow;
};

int main() {
	return verifyDependentAliasLayout<char>() |
		verifyDependentAliasLayout<long long>() |
		verifyDependentAliasLayout<Mixed>();
}
