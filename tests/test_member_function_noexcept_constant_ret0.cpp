// A member function's constant noexcept operand must be folded so is_noexcept
// reflects the operand value rather than the keyword being present. Before this
// was fixed `void f() noexcept(false)` was reported noexcept(true).
struct Probe {
	void throwing() noexcept(false) {}
	void nothrowing() noexcept(true) {}
};

static_assert(!noexcept(((Probe*)nullptr)->throwing()), "noexcept(false) member function is throwing");
static_assert(noexcept(((Probe*)nullptr)->nothrowing()), "noexcept(true) member function is noexcept");

int main() {
	return noexcept(((Probe*)nullptr)->throwing()) ? 1 : 0;
}
