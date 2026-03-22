// Test: struct constructor calls and member access inside constexpr function bodies.
// Also tests returning structs from constexpr functions.

struct Point {
    int x, y;
    constexpr Point(int x, int y) : x(x), y(y) {}
};

struct Pair {
    int a, b;
    constexpr Pair(int a, int b) : a(a), b(b) {}
    constexpr int sum() const { return a + b; }
};

// Test 1: Create and return struct from constexpr function
constexpr Point make_point(int a, int b) { return Point{a, b}; }
constexpr Point pt = make_point(3, 4);
static_assert(pt.x == 3);
static_assert(pt.y == 4);

// Test 2: Create local struct in constexpr function and use it
constexpr int use_local_struct() {
    Point p = Point{5, 6};
    return p.x + p.y;
}
static_assert(use_local_struct() == 11);

// Test 3: Return struct and call member function on it
constexpr Pair make_pair(int a, int b) { return Pair{a, b}; }
constexpr int test_pair_sum() {
    Pair p = make_pair(10, 20);
    return p.sum();
}
static_assert(test_pair_sum() == 30);

int main() { return 0; }
