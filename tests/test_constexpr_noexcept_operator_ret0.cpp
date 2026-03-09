// Test constexpr evaluator support for noexcept(expr)

void may_throw() { }
void no_throw() noexcept { }
void explicit_throwing() noexcept(false) { }

static_assert(!noexcept(may_throw()), "plain function should not be noexcept");
static_assert(noexcept(no_throw()), "noexcept function should be noexcept");
static_assert(!noexcept(explicit_throwing()), "noexcept(false) should not be noexcept");

int main() {
	return (noexcept(may_throw()) == false &&
		noexcept(no_throw()) == true &&
		noexcept(explicit_throwing()) == false) ? 0 : 1;
}