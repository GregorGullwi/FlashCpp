// Type-trait normalization must preserve nominal identity for the canonical
// constructor query after it projects an argument type through the legacy form.
struct Copyable {
	Copyable(const Copyable&) noexcept = default;
};

static_assert(__is_constructible(Copyable, const Copyable&));
static_assert(__is_constructible(Copyable, Copyable&));
static_assert(__is_constructible(Copyable, Copyable));
static_assert(!__is_constructible(Copyable, volatile Copyable&));
static_assert(__is_nothrow_constructible(Copyable, const Copyable&));

int main() {
	return 0;
}
