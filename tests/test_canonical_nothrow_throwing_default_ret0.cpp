// A known-throwing omitted constructor default must make the nothrow
// constructibility query false even when the constructor itself is noexcept.
int mayThrow();

struct ThrowingDefault {
	ThrowingDefault(int, int = mayThrow()) noexcept {}
};

struct LiteralDefault {
	LiteralDefault(int, int = 17) noexcept {}
};

static_assert(!__is_nothrow_constructible(ThrowingDefault, int));
static_assert(__is_nothrow_constructible(LiteralDefault, int));

int main() {
	return __is_nothrow_constructible(ThrowingDefault, int) ? 1 : 0;
}
