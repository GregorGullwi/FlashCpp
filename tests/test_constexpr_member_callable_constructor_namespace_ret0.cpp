// Member functions, callable objects, and constructors resolve unqualified
// names in their class's declaration namespace, not in the namespace that
// triggered the constexpr evaluation.

namespace A {
constexpr int k = 8;

struct S {
	int v;
	constexpr S() : v(k) {}
	constexpr int get() const {
		return k;
	}
};

struct F {
	constexpr int operator()() const {
		return k;
	}
};
}

namespace B {
constexpr int k = 80;
constexpr int via_member = A::S{}.get();
constexpr int via_callable = A::F{}();
constexpr A::S object{};
constexpr int via_constructor = object.v;
}

static_assert(B::via_member == 8, "member body uses the class namespace");
static_assert(B::via_callable == 8, "call operator body uses the class namespace");
static_assert(B::via_constructor == 8, "constructor initializer uses the class namespace");

int main() {
	return B::via_member == 8 && B::via_callable == 8 && B::via_constructor == 8 ? 0 : 1;
}
