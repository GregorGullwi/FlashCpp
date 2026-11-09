// A derived class declaration of operator- hides all base-class operator-
// declarations unless a using-declaration explicitly restores the base name.
struct Base {
	int operator-() const { return 1; }
};

struct Derived : Base {
	int operator-() && { return 2; }
};

struct UsingDerived : Base {
	using Base::operator-;
	int operator-() && { return 3; }
};

template<class T>
concept HasUnaryMinus = requires(T value) { -value; };

static_assert(!HasUnaryMinus<Derived>);
static_assert(HasUnaryMinus<UsingDerived>);

int main() {
	Derived derived;
	UsingDerived using_derived;
	return HasUnaryMinus<Derived> || !HasUnaryMinus<UsingDerived> ? 1 : 0;
}
