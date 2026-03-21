// Test consteval non-type template functions.
// C++20 [dcl.consteval]: consteval applies to template function specialisations
// just as to regular functions; every call must be a constant expression.

template<int N>
consteval int doubled() { return N * 2; }

template<int N>
consteval int tripled() { return N * 3; }

constexpr int a = doubled<5>();
static_assert(a == 10, "doubled<5>() must be 10");

constexpr int b = tripled<4>();
static_assert(b == 12, "tripled<4>() must be 12");

int main() {
	// Runtime foldable calls — valid C++20 (all arguments are constant)
	return doubled<5>() + tripled<4>() - 22; // 10 + 12 - 22 == 0
}
