// C++20 [dcl.decl]: parentheses around a declarator-id do not change the
// declarator. int (f)() is a function, int& (r) is a reference, and
// int *(p) is a pointer. A nested group is the same declarator.

int (pick)() {
	return 7;
}

const int& (pick_ref)() {
	static int value = 11;
	return value;
}

int* (indirect)() {
	static int value = 4;
	return &value;
}

int apply(int ((*fn)())) {
	return fn();
}

template<class T>
struct Hold {
	using Type = T;
};

using NestedFn = int ((*)());

int main() {
	int (value) = 5;
	int storage = 9;
	int& (ref) = storage;
	int* (ptr) = &storage;
	int (*nested)() = pick;

	if (pick() != 7) {
		return 1;
	}
	if (pick_ref() != 11) {
		return 2;
	}
	if (*indirect() != 4) {
		return 3;
	}
	if (value != 5) {
		return 4;
	}
	if (ref != 9) {
		return 5;
	}
	if (*ptr != 9) {
		return 6;
	}
	if (apply(nested) != 7) {
		return 7;
	}
	Hold<int ((*)())>::Type held = pick;
	NestedFn aliased = pick;
	if (held() != 7) {
		return 8;
	}
	if (aliased() != 7) {
		return 9;
	}
	return 0;
}
