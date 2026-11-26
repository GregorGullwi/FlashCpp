// A by-value conversion function can initialize a const int& parameter through
// a materialized temporary, but the non-const int& overload is not viable. The
// non-const overload stays visible so the call must pick the const one.
struct ValueHolder {
	char tag;
	long long wide;
	int narrow;

	operator int() const { return narrow; }
};

int read_lvalue(int& value) {
	return value + 100;
}

int read_const(const int& value) {
	return value;
}

int main() {
	ValueHolder holder{2, 7000000000LL, 30};
	int separate = 0;
	return read_const(holder) + read_lvalue(separate);
}
