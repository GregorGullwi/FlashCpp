// Pointer-to-const is a mutable pointer; compound assignment must be allowed.
// (UCRT wmemchr: `wchar_t const* __s = _S; __s += 8;`)
int main() {
	const int vals[4] = {1, 2, 3, 4};
	const int* p = vals;
	p += 1;
	return *p == 2 ? 0 : 1;
}
