// Phase 17: Verify inferExpressionType handles NewExpressionNode.
// new T returns T*, new T[n] returns T*.

int main() {
	int* p = new int(42);
	int result = *p;
	delete p;
	if (result != 42)
		return 1;

	int* arr = new int[3];
	arr[0] = 10;
	arr[1] = 20;
	arr[2] = 30;
	int sum = arr[0] + arr[1] + arr[2];
	delete[] arr;
	if (sum != 60)
		return 2;

	return 0;
}
