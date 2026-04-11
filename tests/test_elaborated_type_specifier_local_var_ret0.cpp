// Tests elaborated type specifier used in local variable declarations.
// C++ allows using 'struct Type', 'union Type', 'class Type' as type specifiers
// in variable declarations (not just definitions/forward declarations).
// e.g., struct Foo f = { 1, 2 };
//       union Data d;
//       struct Point *ptr;
struct Point { int x; int y; };
union Data { int i; float f; };

int main() {
	struct Point p1 = { 10, 20 };
	struct Point p2;
	p2.x = 5;
	p2.y = 15;
	struct Point *ptr = &p1;
	union Data d;
	d.i = 42;

	// 10 + 5 + 20 - 42 + 20 - 13 = 0
	return p1.x + p2.x + ptr->y - d.i + p1.y - 13;
}
