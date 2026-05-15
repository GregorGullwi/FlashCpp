// Regression: three levels of nested class templates, each level's non-type
// parameter must be substituted when the innermost runtime member function
// body is lazily re-parsed.  Outer<4>::Middle<5>::Inner<6>::compute() must
// return 4*100 + 5*10 + 6 == 456.

template <int OuterV>
struct Outer {
	template <int MiddleV>
	struct Middle {
		template <int InnerV>
		struct Inner {
			int compute() { return OuterV * 100 + MiddleV * 10 + InnerV; }
		};
	};
};

int main() {
	Outer<4>::Middle<5>::Inner<6> v;
	return v.compute() == 456 ? 0 : 1;
}
