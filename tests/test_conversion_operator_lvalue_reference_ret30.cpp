// A single conversion function returning int& binds an int& parameter and a
// const int& parameter directly. The referenced member is mutated through the
// non-const alias and read back through both converted references.
struct MixedReferenceHolder {
	char tag;
	int narrow;
	long long wide;

	operator int&() { return narrow; }
};

int read_lvalue(int& value) {
	return value;
}

int read_const(const int& value) {
	return value;
}

int main() {
	MixedReferenceHolder holder{1, 20, 4000000000LL};
	int& alias = holder;
	alias += 10;
	const int& const_alias = holder;
	const int& call_alias = holder;
	return read_lvalue(holder) + read_const(holder) + const_alias + call_alias - 90;
}
