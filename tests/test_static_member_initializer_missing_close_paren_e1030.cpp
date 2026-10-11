// A parenthesized out-of-line static member variable initializer missing its
// closing parenthesis is reported as
// ExpectedCloseParenAfterStaticMemberInitializer (1030).
struct S {
	static int m;
};

int S::m(1;

int main() {
	return 0;
}
