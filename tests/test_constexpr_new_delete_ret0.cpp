// Test: constexpr new/delete (C++20 [expr.const]/p5)
// Dynamically allocated objects whose lifetime falls entirely within a constant
// expression are permitted in C++20.

// Simple new/delete of a fundamental type
constexpr int new_delete_int() {
	int* p = new int(42);
	int v = *p;
	delete p;
	return v;
}
static_assert(new_delete_int() == 42);

// new int() — value-initialization (zero)
constexpr int new_int_zero() {
	int* p = new int();
	int v = *p;
	delete p;
	return v;
}
static_assert(new_int_zero() == 0);

// new int — default-initialization (value-initialized to 0 in constexpr)
constexpr int new_int_default() {
	int* p = new int;
	int v = *p;
	delete p;
	return v;
}
static_assert(new_int_default() == 0);

// Modify through pointer before deleting
constexpr int new_mutate() {
	int* p = new int(10);
	*p = *p + 5;
	int v = *p;
	delete p;
	return v;
}
static_assert(new_mutate() == 15);

// new/delete array
constexpr int new_delete_array() {
	int* arr = new int[5];
	int sum = 0;
	for (int i = 0; i < 5; ++i) {
		arr[i] = i + 1;
		sum += arr[i];
	}
	delete[] arr;
	return sum;	// 1+2+3+4+5 = 15
}
static_assert(new_delete_array() == 15);

// Struct allocated with new
struct Point {
	int x, y;
	constexpr Point(int a, int b) : x(a), y(b) {}
};

constexpr int new_struct() {
	Point* p = new Point(3, 4);
	int result = p->x + p->y;
	delete p;
	return result;
}
static_assert(new_struct() == 7);

// Multiple allocations in the same function
constexpr int new_multiple() {
	int* a = new int(10);
	int* b = new int(20);
	int result = *a + *b;
	delete a;
	delete b;
	return result;
}
static_assert(new_multiple() == 30);

// new inside a loop
constexpr int new_loop() {
	int total = 0;
	for (int i = 1; i <= 3; ++i) {
		int* p = new int(i * 10);
		total += *p;
		delete p;
	}
	return total;  // 10 + 20 + 30 = 60
}
static_assert(new_loop() == 60);

int main() { return 0; }
