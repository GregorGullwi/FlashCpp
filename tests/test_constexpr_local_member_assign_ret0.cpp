// Test: assigning to members of local struct variables inside constexpr functions

struct Pair {
	int a, b;
	constexpr Pair(int a, int b) : a(a), b(b) {}
};

// Test 1: simple member assignment
constexpr int test_simple_assign() {
	Pair p{3, 4};
	p.a = 10;
	return p.a + p.b;  // 14
}
static_assert(test_simple_assign() == 14);

// Test 2: swap via temp variable
constexpr int test_swap() {
	Pair p{3, 4};
	int tmp = p.a;
	p.a = p.b;
	p.b = tmp;
	return p.a - p.b;  // 4 - 3 = 1
}
static_assert(test_swap() == 1);

// Test 3: compound assignment on member
constexpr int test_compound() {
	Pair p{5, 10};
	p.a += 3;
	return p.a;	// 8
}
static_assert(test_compound() == 8);

int main() { return 0; }
