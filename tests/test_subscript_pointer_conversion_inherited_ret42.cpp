// Test built-in subscript via implicit conversion operator inherited from a base class.
// PtrDerived has no conversion operator of its own; PtrBase::operator int*() is
// inherited and must be discovered by findStructPointerConversionOperator when
// building the subscript expression for d[0] and d[1].

struct PtrBase {
	int data[4];
	operator int*() { return &data[0]; }
};

struct PtrDerived : PtrBase {
	// no conversion operator — inherits operator int*() from PtrBase
};

int main() {
	PtrDerived d;
	d.data[0] = 10;
	d.data[1] = 32;
	// d[0] + d[1] = 10 + 32 = 42
	return d[0] + d[1];
}
