// Test basic consteval function used in constant-evaluated contexts.
// C++20 [dcl.consteval]: a consteval (immediate) function must always produce
// a compile-time constant; every invocation is an immediate invocation.

consteval int answer() {
	return 42;
}

// Constexpr variable initialised from consteval — classic usage
constexpr int x = answer();
static_assert(x == 42, "x must be 42");

// static_assert expression is a constant-evaluated context
static_assert(answer() == 42, "answer() must equal 42");

// Array dimension — constant-evaluated context
int arr[answer()];

// Runtime call that can be constant-folded — valid C++20
int main() {
 // answer() is consteval so the compiler evaluates it to 42 at compile time
	return answer() - 42; // returns 0
}
