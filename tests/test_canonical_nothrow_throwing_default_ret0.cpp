// A known-throwing omitted constructor default must make the nothrow
// constructibility query false even when the constructor itself is noexcept.
int mayThrow();

struct ThrowingDefault {
	ThrowingDefault(int, int = mayThrow()) noexcept {}
};

struct LiteralDefault {
	LiteralDefault(int, int = 17) noexcept {}
};

int values[1];

struct BinaryDefault {
	BinaryDefault(int, int = mayThrow() + 1) noexcept {}
};

struct UnaryDefault {
	UnaryDefault(int, int = -mayThrow()) noexcept {}
};

struct SubscriptDefault {
	SubscriptDefault(int, int = values[mayThrow()]) noexcept {}
};

static_assert(!__is_nothrow_constructible(ThrowingDefault, int));
static_assert(__is_nothrow_constructible(LiteralDefault, int));
static_assert(!__is_nothrow_constructible(BinaryDefault, int));
static_assert(!__is_nothrow_constructible(UnaryDefault, int));
static_assert(!__is_nothrow_constructible(SubscriptDefault, int));

int main() {
	if (__is_nothrow_constructible(ThrowingDefault, int)) return 1;
	if (__is_nothrow_constructible(BinaryDefault, int)) return 2;
	if (__is_nothrow_constructible(UnaryDefault, int)) return 3;
	if (__is_nothrow_constructible(SubscriptDefault, int)) return 4;
	return 0;
}
