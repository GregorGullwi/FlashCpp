// Test: array member brace-init for all native types in constexpr context.
// Covers double, unsigned int, unsigned long long, bool, and char arrays
// with multi-element, single-element, and partial-element brace-init.

// 1. double array - full init
struct DoubleArray {
	double arr[3];
	constexpr DoubleArray() : arr{1.5, 2.5, 3.5} {}
};
constexpr DoubleArray da{};
static_assert(da.arr[0] == 1.5);
static_assert(da.arr[1] == 2.5);
static_assert(da.arr[2] == 3.5);

// 2. double array - single-element; rest must be 0.0
struct DoubleSingle {
	double arr[3];
	constexpr DoubleSingle() : arr{9.9} {}
};
constexpr DoubleSingle ds{};
static_assert(ds.arr[0] == 9.9);
static_assert(ds.arr[1] == 0.0);
static_assert(ds.arr[2] == 0.0);

// 3. double array - partial; remaining elements must be 0.0
struct DoublePartial {
	double arr[4];
	constexpr DoublePartial() : arr{1.0, 2.0} {}
};
constexpr DoublePartial dp{};
static_assert(dp.arr[0] == 1.0);
static_assert(dp.arr[1] == 2.0);
static_assert(dp.arr[2] == 0.0);
static_assert(dp.arr[3] == 0.0);

// 4. unsigned int array - full init
struct UintArray {
	unsigned int arr[3];
	constexpr UintArray() : arr{10u, 20u, 30u} {}
};
constexpr UintArray ua{};
static_assert(ua.arr[0] == 10u);
static_assert(ua.arr[1] == 20u);
static_assert(ua.arr[2] == 30u);

// 5. unsigned long long array - single-element with max value; rest zero
struct UllSingle {
	unsigned long long arr[3];
	constexpr UllSingle() : arr{0xFFFFFFFFFFFFFFFFULL} {}
};
constexpr UllSingle us{};
static_assert(us.arr[0] == 0xFFFFFFFFFFFFFFFFULL);
static_assert(us.arr[1] == 0ULL);
static_assert(us.arr[2] == 0ULL);

// 6. bool array
struct BoolArray {
	bool arr[3];
	constexpr BoolArray() : arr{true, false, true} {}
};
constexpr BoolArray ba{};
static_assert(ba.arr[0] == true);
static_assert(ba.arr[1] == false);
static_assert(ba.arr[2] == true);

// 7. char array - partial; remaining element must be '\0'
struct CharArray {
	char arr[3];
	constexpr CharArray() : arr{'A', 'B'} {}
};
constexpr CharArray ca{};
static_assert(ca.arr[0] == 'A');
static_assert(ca.arr[1] == 'B');
static_assert(ca.arr[2] == '\0');

int main() { return 0; }
