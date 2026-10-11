// An out-of-line static member variable definition missing its terminating
// semicolon is reported as MissingSemicolon (1902).
struct S {
	static int m;
};

int S::m(1)

int main() {
	return 0;
}
