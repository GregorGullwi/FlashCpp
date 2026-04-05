// Test: callable parameter in a generic lambda where the parameter name
// shadows a free function. The parser may resolve the call expression's
// function_declaration() to the free function's DeclarationNode (with a
// different identifier token) while the symbol table holds the lambda
// parameter's DeclarationNode (with the callable struct type). If codegen
// uses the wrong identifier token when constructing the member-style call expression
// for operator(), the emitted IR will reference the free function name
// instead of the parameter variable, producing incorrect code.

int apply(int) { return -1; }  // decoy free function

int main() {
	auto invoke = [](auto apply, int value) {
		return apply(value);
	};

	auto plus_ten = [](int x) { return x + 10; };

	return invoke(plus_ten, 32) == 42 ? 0 : 1;
}
