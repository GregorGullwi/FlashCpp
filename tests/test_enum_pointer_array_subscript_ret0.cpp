// Regression test: array subscript on enum pointer arrays must preserve
// pointer_depth through the IR.  The 4th operand producer in
// generateArraySubscriptIr writes pointer_depth for non-Struct types,
// but toTypedValue interprets it as type_index for Type::Enum, losing
// the pointer depth and corrupting downstream codegen.

enum Color { Red, Green, Blue };

int readColor(Color** colors) {
	return static_cast<int>(*colors[1]);
}

int main() {
	Color a = Green;
	Color b = Blue;
	Color* arr[2] = { &a, &b };

	// arr decays to Color**, subscript should yield Color* (pointer_depth=1),
	// then dereference should yield Color (Blue == 2).
	if (readColor(arr) != 2) return 1;
	return 0;
}
