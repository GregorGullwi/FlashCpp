using Row = short[3];

template<int Rows>
using Grid = Row[Rows + 1];

template<int Rows>
using ForwardedGrid = Grid<Rows>;

template<int Rows>
struct Source {
	using Storage = Row[Rows];
	short data[Rows][3];

	operator Storage&() {
		return data;
	}
};

using WideRow = long long[4];

template<class Element, int Rows>
struct Matrix {
	using Storage = Element[Rows];
	Storage data;
};

int main() {
	Grid<1> grid{{1, 2, 3}, {4, 5, 6}};
	if (sizeof(grid[0]) != sizeof(Row) || grid[1][2] != 6) {
		return 1;
	}

	ForwardedGrid<3> larger{};
	larger[3][2] = 19;
	if (sizeof(larger) != 4 * sizeof(Row) || larger[3][2] != 19) {
		return 2;
	}

	Source<2> source{{{10, 20, 30}, {40, 50, 60}}};
	if (sizeof(source[0]) != sizeof(Row) || source[1][2] != 60) {
		return 3;
	}
	source[1][2] = 42;
	if (source.data[1][2] != 42 || source.data[0][2] != 30) {
		return 4;
	}

	Matrix<WideRow, 2> matrix{};
	matrix.data[1][3] = 77;
	return sizeof(matrix.data[1]) == sizeof(WideRow) && matrix.data[1][3] == 77 ? 0 : 5;
}
