// using Base::operator= restores the nominated base assignment overload after a
// derived assignment operator hides the base name.
struct NonConvertible {
	NonConvertible() = delete;
};

struct Base {
	int value = 0;
	Base& operator=(int input) {
		value = input;
		return *this;
	}
};

struct Derived : Base {
	using Base::operator=;
	Derived& operator=(NonConvertible) { return *this; }
};

struct HiddenDerived : Base {
	HiddenDerived& operator=(NonConvertible) { return *this; }
};

template<class T>
concept AssignsInt = requires(T& value) { value = 1; };

static_assert(!AssignsInt<HiddenDerived>);
static_assert(AssignsInt<Derived>);

int main() {
	Derived value;
	value = 7;
	return value.value == 7 && !AssignsInt<HiddenDerived> ? 0 : 1;
}
