// Test operator[] returning a reference (lvalue) — write through subscript.
// Verifies that the sema-resolved operator[] path correctly handles
// operator[] that returns a reference, enabling assignment through it.

struct RefArray {
	int data[4];

	int& operator[](int index) {
		return data[index];
	}
};

int main() {
	RefArray ra;
	ra.data[0] = 0;
	ra.data[1] = 0;
	ra.data[2] = 0;
	ra.data[3] = 0;
	// Write through operator[]
	ra[0] = 10;
	ra[1] = 20;
	ra[2] = 12;
	// Read back through operator[]
	return ra[0] + ra[1] + ra[2]; // 10 + 20 + 12 = 42
}
