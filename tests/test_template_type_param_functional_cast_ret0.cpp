// Test: Template type parameter in functional-style cast / temporary construction
// Pattern: _Tp(args) in template function body parsed as constructor call,
// not variable declaration. This was previously failing with "Expected identifier".
struct Holder {
	int val;
	Holder(int v) : val(v) {}
};

template <typename _Tp>
struct Maker {
	static int create() {
		_Tp obj(42);
		return obj.val;
	}
};

int main() {
	return Maker<Holder>::create() == 42 ? 0 : 1;
}
