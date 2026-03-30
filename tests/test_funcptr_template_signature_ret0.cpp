// Test: function pointer type through template instantiation preserves
// function_signature for Itanium name mangling.
// Regression test for copy_indirection_from() and TemplateTypeArg
// not carrying function_signature through template instantiation.

typedef int (*FuncPtr)(int);

template<typename T>
struct Holder {
	T value;
};

int main() {
	Holder<FuncPtr> h;
	(void)h;
	return 0;
}
