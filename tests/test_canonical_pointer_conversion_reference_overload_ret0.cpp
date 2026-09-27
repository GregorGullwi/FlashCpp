struct Base {};
struct Derived : Base {};

struct DerivedSelection {};
struct BaseSelection {};
struct VoidSelection {};

template <class Left, class Right>
struct SameType {
	static constexpr bool value = false;
};

template <class Type>
struct SameType<Type, Type> {
	static constexpr bool value = true;
};

DerivedSelection choose(const Derived* const&);
BaseSelection choose(const Base* const&);
VoidSelection choose(const void* const&);
BaseSelection chooseBaseOnly(const Base* const&);
VoidSelection chooseVoidOnly(const void* const&);

Derived* derivedPointer;
Base* basePointer;

static_assert(SameType<decltype(choose(derivedPointer)), DerivedSelection>::value);
static_assert(SameType<decltype(chooseBaseOnly(derivedPointer)), BaseSelection>::value);
static_assert(SameType<decltype(chooseVoidOnly(derivedPointer)), VoidSelection>::value);
static_assert(SameType<decltype(choose(basePointer)), BaseSelection>::value);

int main() {
	return 0;
}
