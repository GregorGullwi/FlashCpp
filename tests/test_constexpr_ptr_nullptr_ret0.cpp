// Test constexpr pointer null checks and comparisons

constexpr int val = 42;
constexpr int other_val = 99;
constexpr const int* ptr = &val;
constexpr const int* ptr2 = &val;
constexpr const int* ptr3 = &other_val;

// ptr == nullptr → false (valid pointer is non-null)
static_assert(!(ptr == nullptr));

// ptr != nullptr → true
static_assert(ptr != nullptr);

// nullptr == ptr → false
static_assert(!(nullptr == ptr));

// nullptr != ptr → true
static_assert(nullptr != ptr);

// Pointer equality: same variable
static_assert(ptr == ptr2);
static_assert(!(ptr != ptr2));

// Pointer inequality: different variables
static_assert(ptr != ptr3);
static_assert(!(ptr == ptr3));

// Logical NOT: !ptr → false (non-null pointer is truthy)
static_assert(!(!ptr));

// Boolean truthiness in if-like context: (ptr && true) == true
static_assert(ptr && true);

// Logical OR: (ptr || false) == true
static_assert(ptr || false);

// Mixed logical: (false && ptr) == false
static_assert(!(false && ptr));

// Mixed logical: (true && ptr) == true
static_assert(true && ptr);

// ptr == nullptr inside a constexpr function
constexpr bool is_null(const int* p) {
	return p == nullptr;
}
static_assert(!is_null(&val));

constexpr bool is_non_null(const int* p) {
	return p != nullptr;
}
static_assert(is_non_null(&val));

// Conditional using ptr in boolean context
constexpr int ptr_or_default(const int* p, int def) {
	if (p) return *p;
	return def;
}
static_assert(ptr_or_default(&val, 0) == 42);

// Logical not on pointer
constexpr bool not_null(const int* p) {
	return !(!p);
}
static_assert(not_null(&val));

int main() {
	return 0;
}
