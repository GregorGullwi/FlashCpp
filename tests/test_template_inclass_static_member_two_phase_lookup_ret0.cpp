// In-class template static member initializer should keep definition-time lookup.
// Post-definition overloads must not replace the already-recorded non-dependent call.

int select_overload(long) {
	return 1;
}

template <typename T>
struct Holder {
	static constexpr int value = select_overload(0);
};

int select_overload(int) {
	return 2;
}

int select_overload(short) {
	return 3;
}

int select_overload(unsigned) {
	return 4;
}

int main() {
	int a = Holder<int>::value;
	int b = Holder<short>::value;
	int c = Holder<unsigned>::value;
	return (a == 1 && b == 1 && c == 1) ? 0 : 1;
}
