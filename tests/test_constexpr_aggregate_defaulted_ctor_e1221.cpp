// C++20 aggregate rules: an explicitly defaulted constructor is still
// user-declared, so the type is not an aggregate and cannot be list-initialized
// from aggregate member clauses.

struct Defaulted {
	int value;
	constexpr Defaulted() = default;
};

static_assert(!__is_aggregate(Defaulted));

constexpr Defaulted bad{42};	// ERROR: not an aggregate, no one-arg constructor

int main() {
	return bad.value;
}

