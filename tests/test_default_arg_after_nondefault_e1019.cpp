// [dcl.fct.default]/4: once a parameter has a default argument, every following
// parameter must also have one. This must surface the originating declaration
// rejection instead of the expression-statement fallback's syntax error.
void f(int a = 10, int b);

int main() {
	return 0;
}
