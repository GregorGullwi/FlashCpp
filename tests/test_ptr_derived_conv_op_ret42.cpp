struct PtrBase {
	int data[3];
	operator int*() { return data; }
};

struct PtrDerived : PtrBase {
	// no conversion operator — inherits from PtrBase
};

int main() {
	PtrDerived d;
	d.data[0] = 10;
	d.data[1] = 32;
	// d[0] + d[1] = 10 + 32 = 42
	return d[0] + d[1];
}
