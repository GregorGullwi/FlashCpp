// A function declaration not followed by a body or a semicolon is reported as
// ExpectedFunctionBodyOrSemicolon (1029).
struct S {
	int f();
};

int S::f() 5;

int main() {
	return 0;
}
