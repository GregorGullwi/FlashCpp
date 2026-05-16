// Regression: the builtin __va_start fallback must not form an ambiguous
// overload set with the UCRT-style declaration `void __va_start(va_list*, ...)`.
// The real-world expansion in corecrt_wstdio.h passes a struct pointer alias as
// the second argument, so the header declaration should win over the builtin
// instead of producing an ambiguity.

typedef char* va_list;

struct LocaleTag {
	int marker;
};

template <typename T>
struct __vcrt_va_list_is_reference {
	enum : bool { __the_value = false };
};

template <typename T>
struct __vcrt_assert_va_start_is_not_reference {
	static_assert(!__vcrt_va_list_is_reference<T>::__the_value, "reference types are invalid");
};

void __va_start(va_list*, ...);

#define __crt_va_start(ap, x) ((void)(__vcrt_assert_va_start_is_not_reference<decltype(x)>(), ((void)(__va_start(&ap, x)))))

int wrapper(LocaleTag* const locale) {
	va_list args = 0;
	__crt_va_start(args, locale);
	return 0;
}

int main() {
	LocaleTag value{42};
	return wrapper(&value);
}
