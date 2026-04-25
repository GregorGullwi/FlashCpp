// Explicitly defaulted default constructors disqualify aggregate init, but
// value-initialization through the default constructor remains valid.

struct Defaulted {
	int value = 42;
	constexpr Defaulted() = default;
};

static_assert(!__is_aggregate(Defaulted));

constexpr Defaulted ok{};
static_assert(ok.value == 42);

int main() {
	return ok.value == 42 ? 0 : 1;
}

