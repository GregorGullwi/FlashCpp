// Regression test: out-of-line member function definitions (delayed body parsing)
// Verifies parameter registration and 'this' injection for out-of-line definitions.

struct Vec2 {
int x;
int y;

Vec2(int a, int b);
int dot(const Vec2& other) const;
void scale(int factor);
};

Vec2::Vec2(int a, int b) : x(a), y(b) {}

int Vec2::dot(const Vec2& other) const {
return x * other.x + y * other.y;
}

void Vec2::scale(int factor) {
x *= factor;
y *= factor;
}

int main() {
Vec2 a(2, 3);
Vec2 b(4, 5);
if (a.dot(b) != 23) return 1;  // 2*4 + 3*5 = 23
a.scale(3);
if (a.x != 6) return 2;
if (a.y != 9) return 3;
return 0;
}
