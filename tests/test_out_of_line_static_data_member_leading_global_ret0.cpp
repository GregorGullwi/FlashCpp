// A leading `::` before an out-of-line declarator-id names the global scope, so
// the owner is resolved from the global namespace even when the definition
// appears inside a namespace. Scalar and pointer-to-array members are written
// and read so a definition that only parses leaves no usable storage.

struct Global {
	static int data;
	static int (*table)[2];
};

int ::Global::data = 7;
int rows[2] = {11, 13};
int (*::Global::table)[2] = &rows;

int other_rows[2] = {19, 23};

namespace M {
struct Local {
	static int data;
	static int (*table)[2];
};

// The definition lives in the class's own namespace but is still written with a
// leading `::`, so the owner spelling must stay anchored at the global scope.
int ::M::Local::data = 17;
int (*::M::Local::table)[2] = &other_rows;
}

namespace Inner {
namespace Deep {
struct Nested {
	static int data;
};
}
}

int ::Inner::Deep::Nested::data = 29;

int main() {
	if (Global::data != 7) {
		return 1;
	}
	if ((*Global::table)[0] != 11 || (*Global::table)[1] != 13) {
		return 2;
	}
	if (M::Local::data != 17) {
		return 3;
	}
	if ((*M::Local::table)[0] != 19 || (*M::Local::table)[1] != 23) {
		return 4;
	}
	if (Inner::Deep::Nested::data != 29) {
		return 5;
	}

	Global::data = 31;
	M::Local::data = 37;
	(*Global::table)[1] = 41;
	(*M::Local::table)[0] = 43;
	if (Global::data != 31 || M::Local::data != 37) {
		return 6;
	}
	if ((*Global::table)[0] != 11 || (*Global::table)[1] != 41) {
		return 7;
	}
	if ((*M::Local::table)[0] != 43 || (*M::Local::table)[1] != 23) {
		return 8;
	}
	return 0;
}
