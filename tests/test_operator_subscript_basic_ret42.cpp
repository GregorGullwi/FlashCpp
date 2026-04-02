// Test basic operator[] on a struct — sema-resolved subscript operator.
// Verifies that ArraySubscriptNode on a struct type is resolved to operator[]
// by SemanticAnalysis::tryResolveSubscriptOperator and dispatched to member
// function call IR by the code generator.

struct IntArray {
	int data[4];

	int operator[](int index) {
		return data[index];
	}
};

int main() {
	IntArray arr;
	arr.data[0] = 10;
	arr.data[1] = 20;
	arr.data[2] = 12;
	arr.data[3] = 40;
	// arr[0] + arr[2] = 10 + 12 = 22; arr[1] = 20; total = 42
	return arr[0] + arr[1] + arr[2];
}
