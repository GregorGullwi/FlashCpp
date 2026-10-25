struct Base { constexpr int operator-() const { return 42; } };
struct Derived : Base {};
static_assert(-Base{} == 42);
static_assert(-Derived{} == 42);

namespace Custom {
	struct Value {};
	constexpr long long operator+(Value) { return 37; }
	constexpr int operator~(Value) { return 19; }
}
static_assert(+Custom::Value{} == 37);
static_assert(~Custom::Value{} == 19);

int main() { return 0; }
