// Test const operator[] on a struct — sema resolves const-qualified subscript.
// Verifies that a const member function operator[] is correctly resolved and
// dispatched through the sema operator[] resolution path.

struct ConstArray {
	int data[4];

	int operator[](int index) const {
		return data[index];
	}
};

int getValue(const ConstArray& arr, int i) {
	return arr[i];
}

int main() {
	ConstArray ca;
	ca.data[0] = 10;
	ca.data[1] = 32;
	ca.data[2] = 99;
	ca.data[3] = 77;
	// getValue calls operator[] on a const reference
	return getValue(ca, 0) + getValue(ca, 1); // 10 + 32 = 42
}
