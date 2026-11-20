// A constexpr function body keeps its lexical static-local binding when the
// name also exists in the function's declaration namespace.

namespace N {
constexpr int k = 5;
int f() {
	static constexpr int k = 9;
	return k;
}
}

int main() {
	return N::f();
}
