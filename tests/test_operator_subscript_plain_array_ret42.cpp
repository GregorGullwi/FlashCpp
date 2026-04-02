// Test that plain array subscript (non-struct) still works after the
// operator[] sema refactor. ArraySubscriptNode on a raw array should
// NOT be resolved to operator[] — it should fall through to the
// built-in pointer arithmetic codegen path.

int main() {
	int arr[4];
	arr[0] = 10;
	arr[1] = 20;
	arr[2] = 12;
	arr[3] = 99;
	return arr[0] + arr[1] + arr[2]; // 10 + 20 + 12 = 42
}
