// A derived declaration of operator+ hides base-class operator+ overloads unless
// a using-declaration explicitly restores the base name.
struct NonConvertible {
	NonConvertible() = delete;
};

struct Base {
	int operator+(int) const { return 1; }
};

struct Derived : Base {
	int operator+(NonConvertible) { return 2; }
};

struct UsingDerived : Base {
	using Base::operator+;
	int operator+(NonConvertible) { return 3; }
};

template<class T>
concept HasBinaryPlus = requires(T value) { value + 1; };

static_assert(!HasBinaryPlus<Derived>);
static_assert(HasBinaryPlus<UsingDerived>);

int main() {
	return HasBinaryPlus<Derived> || !HasBinaryPlus<UsingDerived> ? 1 : 0;
}
