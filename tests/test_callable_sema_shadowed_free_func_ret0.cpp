// Regression test: callable-object operator() resolution via sema correctly
// handles a struct-type callable that shadows a free function with the same
// name (non-lambda version of the generic-lambda shadowing test).
//
// Without sema pre-resolution, codegen looked up the callee by name in the
// symbol table and could resolve to the wrong declaration.  With sema, the
// local-scope stack correctly identifies the struct-typed variable and
// selects the struct's operator() overload.

int process(int x) { return -99; }  // decoy free function

struct Increment {
	int step;
	int operator()(int x) { return x + step; }
};

int call_it(Increment process, int value) {
 // 'process' here is the struct parameter, not the free function above.
 // sema must use the struct's operator() rather than the free function.
	return process(value);
}

int main() {
	Increment inc;
	inc.step = 10;
	int result = call_it(inc, 32);
	return result == 42 ? 0 : 1;
}
