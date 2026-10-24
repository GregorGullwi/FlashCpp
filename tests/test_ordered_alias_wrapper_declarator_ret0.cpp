// An ordered declarator written around an alias keeps that alias's array,
// reference, or member-pointer wrapper as the inner type. Pointee sizes are
// unevaluated so the test does not store through an interleaved member.

struct Owner {
	int value;
};

using Row = int[4];
using Field = int Owner::*;
using Ref = int&;
using Nested = int (*[2])[4];

int object = 7;

Ref read() {
	return object;
}

int main() {
	Row (*grid)[2] = nullptr;
	if (sizeof(*grid) != 2 * 4 * sizeof(int)) {
		return 1;
	}
	Field (*slots)[2] = nullptr;
	if (sizeof(*slots) != 2 * sizeof(Field)) {
		return 2;
	}
	Ref (*reader)() = read;
	if (sizeof(reader) != sizeof(void*)) {
		return 3;
	}
	if (reader() != 7) {
		return 4;
	}
	Nested (*page)[3] = nullptr;
	if (sizeof(*page) != 3 * 2 * sizeof(int*)) {
		return 5;
	}
	return 0;
}
