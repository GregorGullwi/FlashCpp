// Regression test: pack expansion in MemberFunctionCallNode arguments.
// obj.method(args...) inside a variadic template body must expand the
// PackExpansionExprNode into individual arguments during substitution.
// Previously only FunctionCallNode and ConstructorCallNode had this
// expansion; MemberFunctionCallNode was missing it, leaving an unexpanded
// PackExpansionExprNode that would hit an InternalError in codegen.

struct Adder {
	int sum(int a) { return a; }
	int sum(int a, int b) { return a + b; }
	int sum(int a, int b, int c) { return a + b + c; }
};

template<typename... Args>
struct Invoker {
	Adder adder;
	int invoke(Args... args) {
		return adder.sum(args...);
	}
};

int main() {
	Invoker<int> i1;
	if (i1.invoke(10) != 10) return 1;

	Invoker<int, int> i2;
	if (i2.invoke(3, 4) != 7) return 2;

	Invoker<int, int, int> i3;
	if (i3.invoke(1, 2, 3) != 6) return 3;

	return 0;
}
