using Row = short[3];
using Grid = Row[2];
typedef Row TypedefGrid[2];

struct Ordinary {
	short data[2][3];

	operator Grid&() {
		return data;
	}
};

struct Templated {
	short data[2][3];

	template<class Tag = void>
	operator TypedefGrid&() {
		return data;
	}
};

int main() {
	Grid grid{{1, 2, 3}, {4, 5, 6}};
	if (sizeof(grid[0]) != sizeof(Row) || grid[1][2] != 6) {
		return 1;
	}

	Ordinary ordinary{{{10, 20, 30}, {40, 50, 60}}};
	if (sizeof(ordinary[0]) != sizeof(Row) || ordinary[1][2] != 60) {
		return 2;
	}

	Templated templated{{{11, 22, 33}, {44, 55, 66}}};
	if (sizeof(templated[0]) != sizeof(Row) || 1[templated][2] != 66) {
		return 3;
	}
	1[templated][2] = 42;
	return templated.data[1][2] == 42 && templated.data[0][2] == 33 ? 0 : 4;
}
