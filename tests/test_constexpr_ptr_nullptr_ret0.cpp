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
	if (p)
		return *p;
	return def;
}
static_assert(ptr_or_default(&val, 0) == 42);

// Logical not on pointer
constexpr bool not_null(const int* p) {
	return !(!p);
}
static_assert(not_null(&val));

// ===== Short-circuit evaluation tests =====
// These test that && and || correctly skip the RHS when the LHS determines
// the result.  Without short-circuit, the RHS would dereference a non-pointer
// (nullptr is represented as integer 0) and produce an error.

// p && *p: when p is null (integer 0), *p must NOT be evaluated
constexpr bool safe_deref_and(const int* p) {
	return p && (*p == 42);
}
static_assert(safe_deref_and(&val));	 // non-null: evaluates both sides → true
static_assert(!safe_deref_and(nullptr)); // null: short-circuits → false

// !(p) || fallback: when p is non-null, !p is false, so RHS must be evaluated
// when p is null, !p is true, so RHS must NOT be evaluated
constexpr bool null_or_fallback(const int* p) {
	return !p || (*p == 99);
}
static_assert(!null_or_fallback(&val));	// !(&val) is false, *p==99 is false → false
static_assert(null_or_fallback(nullptr));  // !nullptr is true, short-circuits → true

// Chained: (p != nullptr) && (*p > 0) — classic null-guard idiom
constexpr bool guard_and_check(const int* p) {
	return (p != nullptr) && (*p > 0);
}
static_assert(guard_and_check(&val));	  // 42 > 0 → true
static_assert(!guard_and_check(nullptr)); // short-circuits → false

// Short-circuit with function calls on the RHS
constexpr int deref_value(const int* p) {
	return *p;
}
constexpr bool safe_call_and(const int* p) {
	return (p != nullptr) && (deref_value(p) == 42);
}
static_assert(safe_call_and(&val));
static_assert(!safe_call_and(nullptr));

// || short-circuit: true || <anything> → true without evaluating RHS
constexpr bool true_or_deref(const int* p) {
	return (p == nullptr) || (*p == 42);
}
static_assert(true_or_deref(nullptr));  // LHS true, short-circuits
static_assert(true_or_deref(&val));		// LHS false, evaluates RHS: 42==42 → true

// ===== Chained && and || tests =====
// Verify that chained logical operators short-circuit correctly at each level.
// The parser produces left-associative trees: (a && b) && c, (a || b) || c.

// Chained &&: p && q && (*p + *q == 141)
// If either p or q is null, the dereferences must not be evaluated.
constexpr bool chain_and(const int* p, const int* q) {
	return p && q && (*p + *q == 141);
}
static_assert(chain_and(&val, &other_val));	// 42 + 99 == 141 → true
static_assert(!chain_and(nullptr, &other_val)); // first && short-circuits → false
static_assert(!chain_and(&val, nullptr));		  // second && short-circuits → false
static_assert(!chain_and(nullptr, nullptr));	 // first && short-circuits → false

// Chained ||: (!p) || (!q) || (*p + *q == 0)
// If either pointer is null, the || chain should produce true before reaching *p/*q.
constexpr bool chain_or(const int* p, const int* q) {
	return !p || !q || (*p + *q == 141);
}
static_assert(chain_or(nullptr, &other_val));  // !p is true, short-circuits → true
static_assert(chain_or(&val, nullptr));		// !p false, !q true, short-circuits → true
static_assert(chain_or(nullptr, nullptr));	   // !p is true, short-circuits → true
static_assert(chain_or(&val, &other_val));	   // both false, evaluates sum: 141==141 → true

// Mixed && and ||: (p != nullptr) && ((q != nullptr) || fallback)
// When p is null, the outer && short-circuits; when p is non-null but q is null,
// the inner || must still evaluate the fallback without dereferencing q.
constexpr bool mixed_and_or(const int* p, const int* q, bool fallback) {
	return (p != nullptr) && ((q != nullptr) || fallback);
}
static_assert(!mixed_and_or(nullptr, &val, true));   // outer && short-circuits → false
static_assert(mixed_and_or(&val, nullptr, true));	  // inner || fallback → true
static_assert(!mixed_and_or(&val, nullptr, false));	// inner || fallback false → false
static_assert(mixed_and_or(&val, &other_val, false)); // q non-null → true

int main() {
	return 0;
}
