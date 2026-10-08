// noexcept(static_cast<T>(x)) must consult the selected user-defined conversion
// function's exception specification. Before this was fixed the operand's own
// answer was used, so a throwing conversion operator was reported noexcept.
struct ThrowingConvert {
	operator bool() noexcept(false) { return true; }
};

struct NothrowConvert {
	operator bool() noexcept(true) { return true; }
};

static_assert(!noexcept(static_cast<bool>(*((ThrowingConvert*)nullptr))), "throwing conversion operator");
static_assert(noexcept(static_cast<bool>(*((NothrowConvert*)nullptr))), "noexcept conversion operator");
static_assert(noexcept(static_cast<int>(3.5)), "built-in cast stays noexcept");

int main() {
	ThrowingConvert t{};
	NothrowConvert n{};
	(void)t;
	(void)n;
	return 0;
}
