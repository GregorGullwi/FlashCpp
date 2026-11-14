using Row = short[3];
using Grid = short[2][3];

struct Values {
	Row* data;

	operator Row&() const { return *data; }
};

struct Rows {
	short data[2][3];

	operator Grid&() { return data; }
};

int main() {
	Row values = {4, 5, 6};
	const Values source{&values};
	Row& ref = source;
	const short* ptr = source;
	ref[1] = 42;
	if (ptr != &values[0] || ptr[1] != 42) {
		return 1;
	}

	Rows rows;
	rows.data[1][2] = 6;
	Row* row_ptr = rows;
	return row_ptr[1][2] == 6 ? 0 : 2;
}
