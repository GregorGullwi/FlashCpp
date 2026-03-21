// Test constexpr pointer arithmetic: &arr[i], ptr + n, ptr - n, ptr[i], ptr - ptr
constexpr int arr[] = {10, 20, 30, 40, 50};

// &arr[i] — address of array element
constexpr const int* p0 = &arr[0];
static_assert(*p0 == 10);

constexpr const int* p2 = &arr[2];
static_assert(*p2 == 30);

// ptr + n
static_assert(*(p0 + 0) == 10);
static_assert(*(p0 + 1) == 20);
static_assert(*(p0 + 2) == 30);
static_assert(*(p0 + 4) == 50);

// n + ptr
static_assert(*(2 + p0) == 30);
static_assert(*(0 + p0) == 10);

// ptr - n
static_assert(*(p2 - 1) == 20);
static_assert(*(p2 - 2) == 10);

// ptr - ptr (pointer difference)
static_assert(p2 - p0 == 2);
static_assert(p0 - p0 == 0);
constexpr const int* p4 = &arr[4];
static_assert(p4 - p0 == 4);
static_assert(p0 - p4 == -4);

// ptr[i] (subscript through pointer)
static_assert(p0[0] == 10);
static_assert(p0[1] == 20);
static_assert(p0[2] == 30);
static_assert(p0[3] == 40);
static_assert(p0[4] == 50);

// Pointer relational comparisons
static_assert(p0 < p2);
static_assert(p2 > p0);
static_assert(p0 <= p0);
static_assert(p0 >= p0);
static_assert(p0 <= p2);
static_assert(p2 >= p0);

// Pointer equality with offset
static_assert(p0 + 2 == p2);
static_assert(p2 - 2 == p0);
static_assert(!(p0 == p2));
static_assert(p0 != p2);

int main() {
	return 0;
}
