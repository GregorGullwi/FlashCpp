// Hidden friend operator== defined inside a class body.
// Per C++20 [class.friend]/7, this is a hidden friend — it should only be
// findable via ADL when at least one argument has the associated class type.
//
// Currently, operator hidden friends are silently dropped by the parser
// (src/Parser_Decl_StructEnum.cpp:4138 excludes function_name == "operator").
// This test documents the expected behavior: ADL should find operator==
// because both arguments are of type Widget.
struct Widget {
	int value;
	friend bool operator==(Widget a, Widget b) { return a.value == b.value; }
};
int main() {
	Widget a;
	a.value = 5;
	Widget b;
	b.value = 5;
	if (a == b) return 0;  // ADL should find operator==
	return 1;
}
