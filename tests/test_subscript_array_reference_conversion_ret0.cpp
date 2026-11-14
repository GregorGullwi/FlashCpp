using Row = short[3];
using Grid = short[2][3];

struct RowsBase {
	short data[2][3];

	operator Grid&() { return data; }
	operator const Grid&() const { return data; }
};

struct Rows1 : RowsBase {};
struct Rows2 : Rows1 {};
struct Rows3 : Rows2 {};
struct Rows4 : Rows3 {};
struct Rows5 : Rows4 {};
struct Rows6 : Rows5 {};
struct Rows7 : Rows6 {};
struct Rows8 : Rows7 {};
struct Rows9 : Rows8 {};
struct Rows : Rows9 {};

int main() {
	Rows rows;
	rows.data[0][2] = 3;
	rows[1][2] = 42;
	const Rows& view = rows;
	if (sizeof(view[0]) != sizeof(Row) || rows.data[1][2] != 42) {
		return 1;
	}
	return 1[view][2] == 42 && 0[view][2] == 3 ? 0 : 2;
}
