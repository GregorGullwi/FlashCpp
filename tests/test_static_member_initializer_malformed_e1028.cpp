// A malformed out-of-line static member variable initializer is reported as
// StaticMemberInitializerMalformed (1028).
struct S {
	static int m;
};

int S::m(;

int main() {
	return 0;
}
