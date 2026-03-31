// Regression test for ExprResult migration (Phase 2):
// Global pointer variables must have correct metadata in the 4th operand slot.
// The toTypedValue consumer decodes non-struct metadata as pointer_depth;
// a wrong pointer_depth can corrupt downstream pointer arithmetic codegen.
//
// This test forces the global pointer through toTypedValue by:
// 1. Passing it to a function (function arg path uses toTypedValue)
// 2. Using pointer arithmetic (binary op path uses toTypedValue)

int arr[3] = {10, 20, 42};
int* g_ptr = &arr[0];

int deref(int* p) {
	return *p;
}

int main() {
 // Force g_ptr through toTypedValue via function call
	int a = deref(g_ptr);		  // 10

 // Force g_ptr through toTypedValue via pointer arithmetic (binary op)
	int* p2 = g_ptr + 2;
	int b = *p2;				   // 42

 // Combine: 10 + (42 - 10) = 42
	return a + (b - a);
}
