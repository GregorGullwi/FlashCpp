// sizeof of a dependent array must come from the canonical array shape, not a
// flat extent read. The alias form `using array_type = T[5]` resolves to the
// placeholder T, which has no entity while the template is being defined, and
// an omitted-bound deduction must not fall back to the element size.
//
// The record element keeps the answer distinct from the builtin element size:
// sizeof(Wide) == 16 while sizeof(int) == 4.

struct Wide {
	int a;
	double b;
};

template <typename T>
struct AliasSized {
	using array_type = T[5];
	using grid_type = T[2][3];

	static constexpr unsigned long long array_bytes = sizeof(array_type);
	static constexpr unsigned long long grid_bytes = sizeof(grid_type);
};

template <typename T>
constexpr unsigned long long dependent_array_bytes() {
	return sizeof(T[5]);
}

template <typename T>
constexpr unsigned long long dependent_grid_bytes() {
	return sizeof(T[2][3]);
}

int main() {
	int result = 0;
	if (AliasSized<int>::array_bytes != 20) result |= 1;
	if (AliasSized<Wide>::array_bytes != sizeof(Wide) * 5) result |= 2;
	if (AliasSized<Wide>::array_bytes != 80) result |= 4;
	if (AliasSized<int>::grid_bytes != 24) result |= 8;
	if (AliasSized<Wide>::grid_bytes != sizeof(Wide) * 6) result |= 16;
	if (AliasSized<Wide>::grid_bytes != 96) result |= 32;
	if (dependent_array_bytes<int>() != 20) result |= 64;
	if (dependent_array_bytes<Wide>() != sizeof(Wide) * 5) result |= 128;
	if (dependent_grid_bytes<int>() != 24) result |= 256;
	if (dependent_grid_bytes<Wide>() != sizeof(Wide) * 6) result |= 512;
	return result;
}
