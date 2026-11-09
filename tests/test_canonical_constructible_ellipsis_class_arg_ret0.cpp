// [over.ics.ellipsis]: a variadic constructor matches remaining arguments
// through the ellipsis conversion sequence, including class arguments. Passing
// a class through ... is conditionally supported, so it does not add a copy or
// move requirement; the selected constructor's exception specification decides
// the nothrow answer.
struct TrivialArg {
	int value;
};

struct NontrivialArg {
	NontrivialArg(int) {}
	NontrivialArg(const NontrivialArg&) {}
};

struct SafeVariadic {
	SafeVariadic(int, ...) noexcept {}
};

struct NoisyVariadic {
	NoisyVariadic(int, ...) noexcept(false) {}
};

struct TemplateSafeVariadic {
	template <typename Type>
	TemplateSafeVariadic(Type, ...) noexcept {}
};

struct TemplateNoisyVariadic {
	template <typename Type>
	TemplateNoisyVariadic(Type, ...) noexcept(false) {}
};

template <typename Type, typename First, typename Second>
concept ConstructibleFromTwo = __is_constructible(Type, First, Second);

template <typename Type, typename First, typename Second>
requires ConstructibleFromTwo<Type, First, Second>
int probe(Type*, First*, Second*) {
	return 1;
}

int probe(...) {
	return 2;
}

static_assert(__is_constructible(SafeVariadic, int, TrivialArg), "class argument through ellipsis");
static_assert(__is_constructible(SafeVariadic, int, NontrivialArg), "nontrivial class argument through ellipsis");
static_assert(__is_nothrow_constructible(SafeVariadic, int, NontrivialArg), "constructor noexcept decides");
static_assert(!__is_nothrow_constructible(NoisyVariadic, int, NontrivialArg), "throwing constructor is not nothrow");
static_assert(!__is_trivially_constructible(SafeVariadic, int, NontrivialArg), "user-provided variadic is not trivial");
static_assert(__is_constructible(TemplateSafeVariadic, int, NontrivialArg), "template class argument through ellipsis");
static_assert(__is_nothrow_constructible(TemplateSafeVariadic, int, NontrivialArg), "template constructor noexcept decides");
static_assert(!__is_nothrow_constructible(TemplateNoisyVariadic, int, NontrivialArg), "template throwing constructor is not nothrow");

int main() {
	if (probe((SafeVariadic*)nullptr, (int*)nullptr, (TrivialArg*)nullptr) != 1) {
		return 1;
	}
	return 0;
}
