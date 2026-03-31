// Test: implicit (compiler-generated) copy constructor works.
// Struct with no user-defined copy/move constructor should be copyable.

struct Point {
	int x;
	int y;
};

int main() {
	Point a;
	a.x = 3;
	a.y = 7;
	Point b = a;	 // uses implicit copy constructor
	return b.x + b.y - 10;  // 3 + 7 - 10 == 0
}
