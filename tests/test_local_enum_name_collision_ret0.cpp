// Test: two functions define local enums with the same unqualified name "Color"
// but different enumerator values.  If gTypesByName uses bare names, the second
// function's enum will collide with the first and get wrong enumerator values.
//
// Expected: each function sees its own enumerator values.
// foo() should return 10, bar() should return 99.
// main() returns foo() + bar() - 109 == 0 on success.

int foo() {
	enum Color { Red = 10,
				 Green = 20,
				 Blue = 30 };
	Color c = Red;
	return c;  // expect 10
}

int bar() {
	enum Color { Red = 99,
				 Green = 88,
				 Blue = 77 };
	Color c = Red;
	return c;  // expect 99
}

int main() {
	return foo() + bar() - 109;
}
