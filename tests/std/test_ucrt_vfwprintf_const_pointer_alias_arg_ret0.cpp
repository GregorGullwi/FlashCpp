// Regression: UCRT __stdio_common_vfwprintf wrapper compatibility.
// In corecrt_wstdio.h, _locale_t is a pointer alias and the inline wrapper
// takes `_locale_t const _Locale`, then passes it to a declaration expecting
// `_locale_t`. The alias-level const is top-level pointer cv and must not be
// treated as const on the pointee.

using _locale_t = void*;
using locale_alias_2 = _locale_t;

int __stdio_common_vfwprintf(_locale_t locale) {
	(void)locale;
	return 0;
}

int wrapper(_locale_t const locale) {
	return __stdio_common_vfwprintf(locale);
}

int wrapper_chain(locale_alias_2 const locale) {
	return __stdio_common_vfwprintf(locale);
}

int wrapper_expanded(void* const locale) {
	return __stdio_common_vfwprintf(locale);
}

int main() {
	return wrapper((_locale_t)0) + wrapper_chain((locale_alias_2)0) + wrapper_expanded((void*)0);
}
