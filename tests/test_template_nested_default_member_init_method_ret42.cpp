// Phase D test: nested struct created via member function.
// Tests that default member initializers work when the nested struct is
// returned from a member function rather than directly constructed.
// Bug fixed: Inner{} inside make() referenced the template pattern's
// nested type instead of the instantiated version, producing a linker error.
template <int N>
struct Outer {
	struct Inner {
		int tag = N;
	};

	Inner make() {
		return Inner{};
	}
};

int main() {
	Outer<42> o;
	Outer<42>::Inner obj = o.make();
	return obj.tag;  // Should be 42
}
