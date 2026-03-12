// Regression: constexpr function-call member access should keep the resolved
// base owner when an inherited static member is defined out-of-line.
struct Base {
	static const int value;
};

const int Base::value = 42;

struct Derived : Base {};

constexpr Derived makeDerived() {
	return {};
}

static_assert(makeDerived().value == 42, "makeDerived().value should use Base::value");

int main() {
	return makeDerived().value;
}
