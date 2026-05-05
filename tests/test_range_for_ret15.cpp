// Test range-based for loop
int main() {
	int simple[2];
	simple[0] = 10;
	simple[1] = 20;
	int simple_sum = 0;
	for (int x : simple) {
		simple_sum = simple_sum + x;
	}
	if (simple_sum != 30)
		return 1;

	int arr[5] = {1, 2, 3, 4, 5};
	int sum = 0;
	for (int x : arr) {
		sum = sum + x;
	}
	return sum;	// Should return 15
}
