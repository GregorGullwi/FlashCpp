// Test nested struct aggregate initialization at global scope
// Item #8 fix: struct member that is itself a struct can now be initialized with nested InitializerListNode

struct Point {
int x;
int y;
};

struct Line {
Point a;
Point b;
};

Line l = {{1, 2}, {3, 4}};

int main() {
return l.a.x + l.a.y + l.b.x + l.b.y;  // 1+2+3+4 = 10
}
