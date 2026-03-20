// Regression test: constructor with member initializer list (delayed body parsing)
// Verifies that delayed parsing shares the same scope/context setup as immediate parsing.

struct Point {
int x;
int y;

Point(int a, int b) : x(a), y(b) {}

int sum() { return x + y; }
};

int main() {
Point p(3, 7);
if (p.sum() != 10) return 1;
if (p.x != 3) return 2;
if (p.y != 7) return 3;
return 0;
}
