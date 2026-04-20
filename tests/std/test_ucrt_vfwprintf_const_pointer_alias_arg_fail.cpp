// Expected failure: distilled UCRT __stdio_common_vfwprintf overload-resolution
// blocker. In corecrt_wstdio.h, _locale_t is a pointer alias and the inline
// wrapper takes `_locale_t const _Locale`, then passes it to a declaration that
// expects `_locale_t`. FlashCpp currently treats the top-level const on the
// alias as part of the argument type and rejects the call.

using _locale_t = void*;

int __stdio_common_vfwprintf(_locale_t locale);

int wrapper(_locale_t const locale) {
	return __stdio_common_vfwprintf(locale);
}

int main() {
	return 0;
}
