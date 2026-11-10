// A default argument initialized by a call to a known noexcept function with
// literal arguments proves the nothrow construction fact. A throwing target
// remains a known-throwing default.
int make_value() noexcept {
	return 1;
}

int make_throwing_value() noexcept(false) {
	return 1;
}

struct NothrowCallDefault {
	NothrowCallDefault(int, int value = make_value()) noexcept {}
};

struct NothrowRefCallDefault {
	NothrowRefCallDefault(int, const int& value = make_value()) noexcept {}
};

struct ThrowingCallDefault {
	ThrowingCallDefault(int, int value = make_throwing_value()) noexcept {}
};

static_assert(__is_nothrow_constructible(NothrowCallDefault, int), "noexcept call default");
static_assert(__is_nothrow_constructible(NothrowRefCallDefault, int), "noexcept call bound to a reference");
static_assert(!__is_nothrow_constructible(ThrowingCallDefault, int), "throwing call default");

int main() {
	NothrowCallDefault value{1};
	NothrowRefCallDefault reference{1};
	ThrowingCallDefault throwing{1};
	(void)value;
	(void)reference;
	(void)throwing;
	return 0;
}
