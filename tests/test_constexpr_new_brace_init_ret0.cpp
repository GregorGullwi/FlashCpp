// Test: non-array new-expressions support brace initialization in constexpr.

struct Point {
	int x;
	int y;
	constexpr Point(int a, int b) : x(a), y(b) {}
};

struct Counter {
	int value;
	constexpr Counter() : value(9) {}
};

constexpr int test_new_int_value_init() {
	int* p = new int{};
	int value = *p;
	delete p;
	return value;
}

constexpr int test_new_int_direct_init() {
	int* p = new int{42};
	int value = *p + 1;
	delete p;
	return value;
}

constexpr int test_new_struct_brace_ctor() {
	Point* p = new Point{3, 4};
	int value = p->x + p->y;
	delete p;
	return value;
}

constexpr int test_new_struct_brace_default_ctor() {
	Counter* p = new Counter{};
	int value = p->value;
	delete p;
	return value;
}

static_assert(test_new_int_value_init() == 0);
static_assert(test_new_int_direct_init() == 43);
static_assert(test_new_struct_brace_ctor() == 7);
static_assert(test_new_struct_brace_default_ctor() == 9);

int main() {
	return test_new_int_value_init()
		+ test_new_int_direct_init()
		- 43
		+ test_new_struct_brace_ctor()
		- 7
		+ test_new_struct_brace_default_ctor()
		- 9;
}
