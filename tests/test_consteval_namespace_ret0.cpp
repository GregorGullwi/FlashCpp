// Test consteval functions declared inside namespaces.
// C++20 [dcl.consteval]: every call to a consteval function must be a
// constant expression, regardless of where the function is declared.

namespace math {
consteval int square(int n) { return n * n; }
consteval int cube(int n) { return n * n * n; }
} // namespace math

namespace detail {
namespace inner {
consteval int negate(int n) { return -n; }
} // namespace inner
} // namespace detail

constexpr int s = math::square(6);
static_assert(s == 36, "square(6) must be 36");

constexpr int c = math::cube(3);
static_assert(c == 27, "cube(3) must be 27");

constexpr int n = detail::inner::negate(5);
static_assert(n == -5, "negate(5) must be -5");

int main() {
 // runtime calls that are constant-foldable: valid C++20
	int a = math::square(7);	 // folded to 49
	int b = math::cube(2);	   // folded to 8
	return a + b - 57;		   // 49 + 8 - 57 == 0
}
