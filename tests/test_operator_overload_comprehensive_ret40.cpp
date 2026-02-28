// Test comprehensive binary and unary operator overloading on a struct
struct Vec2 {
    int x;
    int y;

    // Binary arithmetic operators
    Vec2 operator+(const Vec2& o) { Vec2 r; r.x = x + o.x; r.y = y + o.y; return r; }
    Vec2 operator-(const Vec2& o) { Vec2 r; r.x = x - o.x; r.y = y - o.y; return r; }
    Vec2 operator*(int s) { Vec2 r; r.x = x * s; r.y = y * s; return r; }

    // Comparison operators
    bool operator==(const Vec2& o) { return x == o.x && y == o.y; }
    bool operator!=(const Vec2& o) { return x != o.x || y != o.y; }
    bool operator<(const Vec2& o) { return (x * x + y * y) < (o.x * o.x + o.y * o.y); }

    // Compound assignment
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
};

// Separate single-member struct for prefix increment (multi-member prefix ++ is a known issue)
struct Counter {
    int val;
    Counter& operator++() { ++val; return *this; }
};

int main() {
    Vec2 a{3, 4};
    Vec2 b{1, 2};

    Vec2 c = a + b;    // {4, 6}
    Vec2 d = a - b;    // {2, 2}
    Vec2 e = a * 2;    // {6, 8}

    int result = 0;

    // Test basic arithmetic
    if (c.x == 4 && c.y == 6) result += 5;
    if (d.x == 2 && d.y == 2) result += 5;
    if (e.x == 6 && e.y == 8) result += 5;

    // Test comparison
    if (a == a) result += 3;
    if (a != b) result += 3;
    Vec2 small{1, 1};
    Vec2 big{10, 10};
    if (small < big) result += 3;

    // Test compound assignment
    Vec2 f{10, 20};
    f += b; // {11, 22}
    if (f.x == 11 && f.y == 22) result += 5;
    f -= b; // {10, 20}
    if (f.x == 10 && f.y == 20) result += 5;

    // Test prefix increment on single-member struct
    Counter g{5};
    ++g; // val becomes 6
    if (g.val == 6) result += 6;

    return result; // 40
}
