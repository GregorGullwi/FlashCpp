// Test: runtime constructor member array single-element brace-init zero-fill.
// e.g., arr{val} for int arr[4] stores val at index 0 and zero-fills the rest.

struct SingleElemRuntime {
	int data[4];
	SingleElemRuntime(int v) : data{v} {}
};

struct DoubleElemRuntime {
	double arr[3];
	DoubleElemRuntime(double d) : arr{d} {}
};

int main() {
	SingleElemRuntime s(42);
	if (s.data[0] != 42)
		return 1;
	if (s.data[1] != 0)
		return 2;
	if (s.data[2] != 0)
		return 3;
	if (s.data[3] != 0)
		return 4;

	DoubleElemRuntime d(3.14);
	if (d.arr[0] != 3.14)
		return 5;
	if (d.arr[1] != 0.0)
		return 6;
	if (d.arr[2] != 0.0)
		return 7;

	return 0;
}
