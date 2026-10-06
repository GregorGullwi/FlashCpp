// The constructible traits have one shared authority, but they are reached
// from three contexts: the folded/constexpr evaluator, the lazy constraint
// evaluator, and code generation. This regression drives the same matrix of
// targets and arguments through all three and requires the answers to agree,
// so a future change that lets one path drift from the shared evaluator is
// caught here rather than in a downstream SFINAE or lowered-constant bug.
struct Plain {
	int value;
	long tag;
};

struct DeletedDefault {
	DeletedDefault() = delete;
};

struct NeedsArgument {
	NeedsArgument(int);
};

struct TwoArgs {
	TwoArgs(int, long);
};

struct Abstract {
	virtual void method() = 0;
};

struct UserDefault {
	UserDefault() {}
};

struct NothrowDefault {
	NothrowDefault() noexcept {}
};

struct ThrowingDefault {
	ThrowingDefault() {}
};

template <class Type>
struct Box {
	Type value;
};

template <typename Type>
concept DefaultConstructible = __is_constructible(Type);

template <typename Type>
concept TriviallyConstructible = __is_trivially_constructible(Type);

template <typename Type>
concept NothrowConstructible = __is_nothrow_constructible(Type);

template <typename Type, typename Arg>
concept ConstructibleFrom = __is_constructible(Type, Arg);

template <typename Type, typename First, typename Second>
concept ConstructibleFromTwo = __is_constructible(Type, First, Second);

template <typename Type>
requires DefaultConstructible<Type>
int lazy_default(Type*) {
	return 1;
}
int lazy_default(...) {
	return 0;
}

template <typename Type>
requires TriviallyConstructible<Type>
int lazy_trivial(Type*) {
	return 1;
}
int lazy_trivial(...) {
	return 0;
}

template <typename Type>
requires NothrowConstructible<Type>
int lazy_nothrow(Type*) {
	return 1;
}
int lazy_nothrow(...) {
	return 0;
}

template <typename Type, typename Arg>
requires ConstructibleFrom<Type, Arg>
int lazy_from(Type*, Arg*) {
	return 1;
}
int lazy_from(...) {
	return 0;
}

template <typename Type, typename First, typename Second>
requires ConstructibleFromTwo<Type, First, Second>
int lazy_from_two(Type*, First*, Second*) {
	return 1;
}
int lazy_from_two(...) {
	return 0;
}

// Folded answers, pinned in a constant context. Bit i is case i.
constexpr int folded_mask() {
	int mask = 0;
	if (__is_constructible(Plain)) mask |= 1;
	if (__is_constructible(DeletedDefault)) mask |= 2;
	if (__is_constructible(NeedsArgument)) mask |= 4;
	if (__is_constructible(NeedsArgument, int)) mask |= 8;
	if (__is_constructible(TwoArgs, int, long)) mask |= 16;
	if (__is_constructible(TwoArgs, int)) mask |= 32;
	if (__is_constructible(Box<int>)) mask |= 64;
	if (__is_constructible(Abstract)) mask |= 128;
	if (__is_trivially_constructible(Plain)) mask |= 256;
	if (__is_trivially_constructible(UserDefault)) mask |= 512;
	if (__is_nothrow_constructible(NothrowDefault)) mask |= 1024;
	if (__is_nothrow_constructible(ThrowingDefault)) mask |= 2048;
	return mask;
}

// The expected answers: Plain, NeedsArgument(int), TwoArgs(int, long),
// Box<int>, trivially Plain, and nothrow NothrowDefault are true; the rest are
// false. Pinned so a change in the shared answer is a compile error here.
constexpr int expected_mask = 1 | 8 | 16 | 64 | 256 | 1024;

static_assert(folded_mask() == expected_mask, "folded constructibility answers changed");

// Lowered answers, read at run time so code generation evaluates the traits.
int codegen_mask() {
	int mask = 0;
	if (__is_constructible(Plain)) mask |= 1;
	if (__is_constructible(DeletedDefault)) mask |= 2;
	if (__is_constructible(NeedsArgument)) mask |= 4;
	if (__is_constructible(NeedsArgument, int)) mask |= 8;
	if (__is_constructible(TwoArgs, int, long)) mask |= 16;
	if (__is_constructible(TwoArgs, int)) mask |= 32;
	if (__is_constructible(Box<int>)) mask |= 64;
	if (__is_constructible(Abstract)) mask |= 128;
	if (__is_trivially_constructible(Plain)) mask |= 256;
	if (__is_trivially_constructible(UserDefault)) mask |= 512;
	if (__is_nothrow_constructible(NothrowDefault)) mask |= 1024;
	if (__is_nothrow_constructible(ThrowingDefault)) mask |= 2048;
	return mask;
}

// Lazy answers, forced through concept requirements during instantiation.
int lazy_mask() {
	Plain* plain = nullptr;
	DeletedDefault* deleted = nullptr;
	NeedsArgument* needs = nullptr;
	TwoArgs* two = nullptr;
	Box<int>* box = nullptr;
	Abstract* abstract = nullptr;
	UserDefault* user = nullptr;
	NothrowDefault* nothrow = nullptr;
	ThrowingDefault* throwing = nullptr;
	int* int_arg = nullptr;
	long* long_arg = nullptr;

	int mask = 0;
	if (lazy_default(plain) == 1) mask |= 1;
	if (lazy_default(deleted) == 1) mask |= 2;
	if (lazy_default(needs) == 1) mask |= 4;
	if (lazy_from(needs, int_arg) == 1) mask |= 8;
	if (lazy_from_two(two, int_arg, long_arg) == 1) mask |= 16;
	if (lazy_from(two, int_arg) == 1) mask |= 32;
	if (lazy_default(box) == 1) mask |= 64;
	if (lazy_default(abstract) == 1) mask |= 128;
	if (lazy_trivial(plain) == 1) mask |= 256;
	if (lazy_trivial(user) == 1) mask |= 512;
	if (lazy_nothrow(nothrow) == 1) mask |= 1024;
	if (lazy_nothrow(throwing) == 1) mask |= 2048;
	return mask;
}

int main() {
	const int codegen = codegen_mask();
	if (codegen != expected_mask) {
		return 1;
	}
	const int lazy = lazy_mask();
	if (lazy != expected_mask) {
		return 2;
	}
	return 0;
}
