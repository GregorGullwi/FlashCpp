// Test C++20 aggregate parenthesized initialization (P0960)
// Expected return: 42

struct Point {
    int x;
    int y;
};

struct Triple {
    int a;
    int b;
    int c;
};

int getX(Point p) { return p.x; }

int main() {
    // Direct aggregate paren init in block scope
    Point p(10, 20);

    // Partial init (only first member, rest zero-initialized)
    Point q(5);

    // Copy init from aggregate paren-init temporary
    Triple t = Triple(1, 2, 3);

    // Expression context: aggregate paren init as function argument
    int val = getX(Point(4, 0));

    return p.x + p.y + q.x + t.c + val;
    // 10 + 20 + 5 + 3 + 4 = 42
}
