namespace demo {
	template <typename T>
	struct __type_identity {
		using type = T;
	};

	// Unbounded arrays are always considered complete-or-unbounded.
	template <typename T>
	constexpr bool __is_complete_or_unbounded(__type_identity<T[]>) {
		return true;
	}

	// Complete types (sizeof succeeds) are considered complete.
	template <typename T>
	constexpr bool __is_complete_or_unbounded(__type_identity<T>)
		requires requires { sizeof(T); } {
		return true;
	}

	// Fallback: incomplete types (sizeof ill-formed) are not complete.
	template <typename T>
	constexpr bool __is_complete_or_unbounded(__type_identity<T>) {
		return false;
	}
}

struct Incomplete;

static_assert(demo::__is_complete_or_unbounded(demo::__type_identity<int>{}));
static_assert(!demo::__is_complete_or_unbounded(demo::__type_identity<Incomplete>{}));
static_assert(!demo::__is_complete_or_unbounded(demo::__type_identity<void>{}));
static_assert(demo::__is_complete_or_unbounded(demo::__type_identity<int[]>{}));
static_assert(demo::__is_complete_or_unbounded(demo::__type_identity<int*>{}));
static_assert(demo::__is_complete_or_unbounded(demo::__type_identity<int&>{}));

int main() {
	return 0;
}
