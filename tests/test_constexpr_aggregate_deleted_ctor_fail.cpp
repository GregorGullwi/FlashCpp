// C++20 aggregate rules: any user-declared constructor, including a deleted
// constructor, makes the type non-aggregate.

struct Deleted {
	int value;
	constexpr Deleted(int) = delete;
};

static_assert(!__is_aggregate(Deleted));

constexpr Deleted bad{42};	// ERROR: deleted/user-declared constructor, not aggregate init

int main() {
	return bad.value;
}

