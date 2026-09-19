// An alias whose target is an array must produce an array object. The extents
// live on the alias type specifier rather than in declarator brackets, so
// `A obj;` previously behaved as a scalar (sizeof == element size, element
// writes out of bounds). Covers a non-template alias and a template alias with
// multi-dimensional extents.
using ConcreteArray = int[3];

template <class T>
using Matrix = T[2][3];

int main() {
	ConcreteArray first = {1, 2, 3};
	Matrix<short> grid = {{1, 2, 3}, {4, 5, 6}};
	const bool sizes_ok = sizeof(first) == 3 * sizeof(int) &&
		sizeof(grid) == 6 * sizeof(short);
	const bool values_ok = first[0] == 1 && first[1] == 2 && first[2] == 3 &&
		grid[0][0] == 1 && grid[0][2] == 3 &&
		grid[1][0] == 4 && grid[1][2] == 6;
	return sizes_ok && values_ok ? 42 : 0;
}
