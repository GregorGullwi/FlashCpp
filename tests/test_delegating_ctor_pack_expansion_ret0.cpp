// Regression test: delegating constructor arguments that contain a
// PackExpansionExprNode must be expanded during template instantiation.
//
// substituteAndCopyInitializers expands PackExpansionExprNode in base
// initializer arguments (via expandPackExpansionArgs), but the delegating
// initializer path only calls substituteTemplateParameters — which returns
// a single node instead of N expanded arguments.
//
// This test triggers the bug:
//   Wrapper<int, char>(args...) : Wrapper(42, args...)
// The "args..." in the delegating initializer is a PackExpansionExprNode.
// With the bug, the pack is not expanded and codegen receives a single
// unexpanded node instead of the individual pack elements.

// Target constructor takes a fixed int + the variadic pack.
// Delegating constructor forwards the pack with an extra leading argument.
template<typename... Args>
struct Wrapper {
	int first;
	int second;
	int third;

	// Target constructor: stores values in members
	Wrapper(int a, int b, int c) : first(a), second(b), third(c) {}

	// Delegating constructor: prepends 100 and forwards the pack
	Wrapper(Args... args) : Wrapper(100, args...) {}
};

int main() {
	// Wrapper<int, int> — pack is [int, int], delegating ctor calls Wrapper(100, 10, 20)
	Wrapper<int, int> w1(10, 20);
	if (w1.first != 100) return 1;
	if (w1.second != 10) return 2;
	if (w1.third != 20) return 3;

	// Direct construction via target constructor (no delegation)
	Wrapper<int, int> w2(1, 2, 3);
	if (w2.first != 1) return 4;
	if (w2.second != 2) return 5;
	if (w2.third != 3) return 6;

	return 0;
}
