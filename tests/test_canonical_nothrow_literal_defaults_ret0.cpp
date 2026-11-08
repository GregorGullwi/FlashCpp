int mayThrow();

struct LiteralDefaults {
	LiteralDefaults(short, double = 1.5, bool = true) noexcept {}
};

struct ThrowingConstructor {
	ThrowingConstructor(int, long = 9) noexcept(false) {}
};

struct MixedDefaults {
	MixedDefaults(short, int = mayThrow(), long long = -17) noexcept {}
};

template<class T>
struct TemplateDefaults {
	TemplateDefaults(T, double = 2.5) noexcept {}
};

template<class T>
concept NothrowFromShort = __is_nothrow_constructible(T, short);

template<class T> requires NothrowFromShort<T>
int select(T*) {
	return 1;
}

int select(...) {
	return 0;
}

static_assert(__is_nothrow_constructible(LiteralDefaults, short));
static_assert(__is_nothrow_constructible(LiteralDefaults, short, double));
static_assert(!__is_nothrow_constructible(ThrowingConstructor, int));
static_assert(__is_nothrow_constructible(MixedDefaults, short, int));
static_assert(__is_nothrow_constructible(TemplateDefaults<int>, int));

int main() {
	LiteralDefaults* literals = nullptr;
	ThrowingConstructor* throwing = nullptr;
	return select(literals) == 1 && select(throwing) == 0 ? 0 : 1;
}
