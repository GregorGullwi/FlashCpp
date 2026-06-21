// Regression: overload resolution must treat the integer literal 0 as a null
// pointer constant for pointer-like parameters without stealing exact integer
// matches from non-pointer overloads.

using va_list = char*;

struct FILE;
struct __crt_locale_pointers;

using _locale_t = __crt_locale_pointers*;
using Null = decltype(nullptr);

int __stdio_common_vfwprintf(
	unsigned long long options,
	FILE* stream,
	wchar_t const* format,
	_locale_t locale,
	va_list args) {
	return (options == 0 && stream == 0 && format == 0 && locale == 0 && args == 0) ? 0 : 7;
}

int _vfwprintf_l(
	FILE* const stream,
	wchar_t const* const format,
	_locale_t const locale,
	va_list args) {
	return __stdio_common_vfwprintf(0, stream, format, locale, args);
}

int vfwprintf(
	FILE* const stream,
	wchar_t const* const format,
	va_list args) {
	return _vfwprintf_l(stream, format, 0, args);
}

int pick(int) {
	return 1;
}

int pick(_locale_t) {
	return 2;
}

int pickNullptr(int) {
	return 3;
}

int pickNullptr(decltype(nullptr)) {
	return 4;
}

int onlyNullptr(decltype(nullptr)) {
	return 5;
}

int bindNullConstRef(const Null& value) {
	return value == nullptr ? 6 : 0;
}

int bindNullRRef(Null&& value) {
	return value == nullptr ? 7 : 0;
}

int main() {
	va_list args = 0;
	if (vfwprintf(0, 0, args) != 0) return 1;
	if (pick(0) != 1) return 2;
	if (pickNullptr(0) != 3) return 3;
	if (pickNullptr(nullptr) != 4) return 4;
	if (onlyNullptr(0) != 5) return 5;
	if (bindNullConstRef(0) != 6) return 6;
	if (bindNullRRef(0) != 7) return 7;
	return 0;
}
