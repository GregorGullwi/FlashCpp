// Regression: decltype(auto) return deduction must combine the expression's
// type with its value category. Dereferencing a pointer is an lvalue, so this
// function returns long long&, not long long. Cover an aggregate as well so
// lowering cannot replace a reference result with an aggregate value.
struct Record {
	int small;
	long long wide;
};

decltype(auto) dereference(long long* pointer) {
	return *pointer;
}

decltype(auto) dereferenceRecord(Record* pointer) {
	return *pointer;
}

int main() {
	long long value = 41;
	dereference(&value) = 42;
	if (value != 42) {
		return 1;
	}

	Record record{3, 19};
	dereferenceRecord(&record).small = 7;
	dereferenceRecord(&record).wide = 23;
	return record.small == 7 && record.wide == 23 ? 0 : 2;
}
