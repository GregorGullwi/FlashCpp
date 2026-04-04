struct S {
	int x;

	constexpr S(int value)
		: x(value) {}
};

struct Base {
	long long y;

	constexpr Base(long long value)
		: y(value) {}
};

struct Derived : Base {
	constexpr Derived(long long value)
		: Base(value) {}
};

union U {
	int as_int;
	float as_float;

	constexpr U(float value)
		: as_float(value) {}
};

constexpr int S::* member = &S::x;
constexpr int S::* null_member = nullptr;
constexpr long long Derived::* inherited_member = &Derived::y;
constexpr float U::* union_member = &U::as_float;

constexpr S object(42);
constexpr Derived derived(17);
constexpr U number(3.5f);

constexpr int readOrFallback(const S& value, int S::* ptr) {
	return ptr == nullptr ? 0 : value.*ptr;
}

constexpr int result =
	(object.*member == 42 &&
	 readOrFallback(object, null_member) == 0 &&
	 derived.*inherited_member == 17 &&
	 number.*union_member > 3.0f &&
	 number.*union_member < 4.0f)
		? 0
		: 1;

static_assert(result == 0);

int main() {
	return result;
}
