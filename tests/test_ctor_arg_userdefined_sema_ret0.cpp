// Regression: constructor argument lowering must consume sema-owned
// StandardConversionKind::UserDefined annotations for conversion operators.

struct BoxedInt {
	int value;
	BoxedInt(int v) : value(v) {}
	operator int() const { return value; }
	operator bool() const { return value != 0; }
};

struct Pair {
	int first;
	bool second;
	Pair(int a, bool b) : first(a), second(b) {}
};

static Pair make_pair(BoxedInt first, BoxedInt second) {
	return Pair(first, second);
}

int main() {
	BoxedInt twenty_one(21);
	BoxedInt zero(0);

	Pair local(twenty_one, zero);
	if (local.first != 21) return 1;
	if (local.second) return 2;

	Pair temporary = make_pair(BoxedInt(7), BoxedInt(3));
	if (temporary.first != 7) return 3;
	if (!temporary.second) return 4;

	return 0;
}
