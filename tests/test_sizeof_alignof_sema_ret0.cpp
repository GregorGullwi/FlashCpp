// Test that sizeof/alignof results participate correctly in sema type inference.
// sizeof and alignof return size_t (unsigned long long on 64-bit), so when
// used in expressions with other types, usual arithmetic conversions apply.

struct TestStruct {
	int a;
	double b;
	char c;
};

int main() {
 // Test 1: sizeof result in arithmetic expression
 // sizeof(int) = 4, should be usable in arithmetic
	int size = sizeof(int) + sizeof(double);	 // 4 + 8 = 12
	if (size != 12)
		return 1;

 // Test 2: alignof result in arithmetic expression
	int align = alignof(double) - alignof(int);	// 8 - 4 = 4
	if (align != 4)
		return 2;

 // Test 3: sizeof expression result
	int x = 42;
	int expr_size = sizeof(x);  // 4
	if (expr_size != 4)
		return 3;

 // Test 4: sizeof in comparison (result should be size_t, larger than int)
	bool bigger = sizeof(double) > sizeof(int);	// 8 > 4 = true
	if (!bigger)
		return 4;

 // Test 5: sizeof struct
 // TestStruct has int(4) + padding(4) + double(8) + char(1) + padding(7) = 24
	int struct_size = sizeof(TestStruct);
	if (struct_size != 24)
		return 5;

	return 0;
}
