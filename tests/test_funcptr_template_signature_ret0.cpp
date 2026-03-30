// Test: function pointer type through template instantiation preserves
// function_signature for Itanium name mangling.
// Regression test for copy_indirection_from() and TemplateTypeArg
// not carrying function_signature through template instantiation.

template<typename F>
struct Wrapper {
	F func;
};

int handler(int x) { return x; }

int main() {
	Wrapper<int(*)(int)> w;
	w.func = handler;
	int result = w.func(0);
	return result;
}
