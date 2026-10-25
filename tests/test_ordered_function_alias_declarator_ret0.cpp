// An ordered declarator around a function alias denotes a pointer or reference
// to that function. Cv written on the function type is ignored.

using F = int(int);
using PF = int (*)(int);

int id(int value) {
	return value + 1;
}

int main() {
	F (*pointer) = id;
	if (pointer(41) != 42) {
		return 1;
	}
	F& reference = id;
	if (reference(41) != 42) {
		return 2;
	}
	const F* const_name = id;
	F* plain = const_name;
	if (plain(41) != 42) {
		return 3;
	}
	PF (*page)[2] = nullptr;
	if (sizeof(*page) != 2 * sizeof(PF)) {
		return 4;
	}
	return 0;
}
