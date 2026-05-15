// Regression: outer non-type template parameter must be substituted when
// a nested struct's runtime member function body is lazily re-parsed.
// Without the fix, codegen would fail with "Symbol 'OuterV' not found".

template <int OuterV>
struct Outer {
	template <int InnerV>
	struct Inner {
		int compute() { return OuterV * 10 + InnerV; }
	};
};

int main() {
	Outer<3>::Inner<5> v;
	return v.compute() == 35 ? 0 : 1;
}
