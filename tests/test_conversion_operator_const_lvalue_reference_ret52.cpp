// A single conversion function returning const int& binds a const int&
// parameter and a const int& variable directly; the value is read through both.
struct ConstReferenceHolder {
	long long wide;
	int narrow;

	operator const int&() const { return narrow; }
};

int read_const(const int& value) {
	return value;
}

int main() {
	ConstReferenceHolder holder{6000000000LL, 26};
	const int& alias = holder;
	return read_const(holder) + alias;
}
