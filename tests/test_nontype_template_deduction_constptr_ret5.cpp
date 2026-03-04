// Test: pre-deduction preserves pointer and CV qualifiers from struct template args.
// When T is deduced as "const int*" from Holder<const int*, 5>, the pointer depth
// and const qualifier must survive the pre-deduction pass. If makeType() were used
// instead of makeTypeSpecifier(), the qualifiers would be lost and T would become
// a bare int (or worse, the pointer depth would vanish), causing the function to
// fail to instantiate or produce wrong results.
template<typename T, int N>
struct Holder {
	T value;
	int tag;
};

template<typename T, int N>
int getTag(Holder<T, N>& h) {
	return N;
}

int main() {
	int x = 42;
	Holder<const int*, 5> h;
	h.tag = 5;
	return getTag(h);  // T=const int*, N=5 deduced; returns 5
}
