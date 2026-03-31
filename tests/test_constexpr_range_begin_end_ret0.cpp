// Test: range-based for over objects with begin()/end() member functions in constexpr context.
// These are medium-complexity constexpr features from docs/CONSTEXPR_LIMITATIONS.md.

// ============================================================
// Part 1: begin() returns member array directly (Case A)
// The evaluator recognises the returned array and iterates it.
// ============================================================

struct IntVec3 {
	int data[3];
	constexpr IntVec3() : data{10, 20, 30} {}
	constexpr int* begin() { return data; }
	constexpr int* end() { return data + 3; }
};

constexpr int sum_intvec3() {
	IntVec3 v{};
	int s = 0;
	for (int x : v)
		s += x;
	return s;
}
static_assert(sum_intvec3() == 60);	// 10+20+30

// break inside range-for over begin()/end() object
constexpr int first_above_15() {
	IntVec3 v{};
	for (int x : v) {
		if (x > 15)
			return x;
	}
	return -1;
}
static_assert(first_above_15() == 20);

// continue inside range-for over begin()/end() object
constexpr int sum_skip_20() {
	IntVec3 v{};
	int s = 0;
	for (int x : v) {
		if (x == 20)
			continue;
		s += x;
	}
	return s;
}
static_assert(sum_skip_20() == 40);	// 10+30

// break exits the range-for loop early
constexpr int sum_until_break_20() {
	IntVec3 v{};
	int s = 0;
	for (int x : v) {
		if (x == 20)
			break;
		s += x;
	}
	return s;
}
static_assert(sum_until_break_20() == 10);

// ============================================================
// Part 2: begin()/end() with explicit pointer syntax (Case B)
// begin() returns &data[0], end() returns &data[size].
// The evaluator resolves the backing array from member bindings.
// ============================================================

struct BoundedVec {
	int data[5];
	int size;
	constexpr BoundedVec() : data{2, 4, 6, 8, 10}, size(3) {}
	constexpr int* begin() { return &data[0]; }
	constexpr int* end() { return &data[size]; }
};

constexpr int sum_bounded() {
	BoundedVec v{};
	int s = 0;
	for (int x : v)
		s += x;
	return s;
}
static_assert(sum_bounded() == 12);	// 2+4+6 (first 3 of 5)

// ============================================================
// Part 3: template struct with begin()/end()
// ============================================================

template <int N>
struct ConstVec {
	int data[N];
	constexpr int* begin() { return data; }
	constexpr int* end() { return data + N; }
};

constexpr int sum_constvec4() {
	ConstVec<4> v{{1, 2, 3, 4}};
	int s = 0;
	for (int x : v)
		s += x;
	return s;
}
static_assert(sum_constvec4() == 10);

// ============================================================
// Part 4: nested range-for over two begin()/end() objects
// ============================================================

struct SmallRange {
	int data[3];
	constexpr SmallRange() : data{1, 2, 3} {}
	constexpr int* begin() { return data; }
	constexpr int* end() { return data + 3; }
};

constexpr int dot_product() {
	SmallRange a{};
	SmallRange b{};
	int s = 0;
	int i = 0;
	for (int x : a) {
		int j = 0;
		for (int y : b) {
			if (i == j)
				s += x * y;	// diagonal: 1*1 + 2*2 + 3*3
			j++;
		}
		i++;
	}
	return s;
}
static_assert(dot_product() == 14);	// 1+4+9

int main() { return 0; }
