// Test: short-circuit && and || in constexpr evaluation
// &&: when LHS is false, RHS must NOT be evaluated
// ||: when LHS is true,  RHS must NOT be evaluated
// These patterns are especially important for null-pointer guards.

// --- top-level static_assert short-circuit ---

constexpr int val = 42;
constexpr const int* valid_ptr = &val;

// || short-circuit: LHS is truthy → should succeed even if RHS would fail
static_assert(true || (1 / 0 == 0));   // RHS is division-by-zero but is never evaluated

// && short-circuit: LHS is false → should succeed even if RHS would fail
static_assert(!(false && (1 / 0 == 0)));

// Pointer guard: p != nullptr && *p == 42
static_assert(valid_ptr != nullptr && *valid_ptr == 42);

// Null pointer case: if p is nullptr, short-circuit prevents invalid dereference.
constexpr const int* null_ptr = nullptr;
static_assert(null_ptr == nullptr || *valid_ptr == 42);

// Chained short-circuit
static_assert(true || false || false);
static_assert(!(false && false && true));  // false && ... = false immediately

// --- constexpr function bodies ---

constexpr int safe_deref(const int* p) {
    if (p != nullptr && *p > 0) return *p;
    return -1;
}

static_assert(safe_deref(valid_ptr) == 42);
static_assert(safe_deref(nullptr)   == -1);

// || short-circuit in function: returns first truthy operand's branch
constexpr bool either_positive(int a, int b) {
    return a > 0 || b > 0;
}
static_assert( either_positive(1,  0));
static_assert( either_positive(0,  1));
static_assert(!either_positive(0,  0));
static_assert( either_positive(-1, 5));

// && short-circuit in function
constexpr bool both_positive(int a, int b) {
    return a > 0 && b > 0;
}
static_assert( both_positive(1, 2));
static_assert(!both_positive(0, 2));
static_assert(!both_positive(1, 0));

int main() { return 0; }
