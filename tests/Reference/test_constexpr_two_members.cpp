// Test constexpr constructor evaluation without inheritance

struct Point {
    int x;
    int y;
    
    constexpr Point(int a, int b) : x(a), y(b) {}
    constexpr ~Point() {}
};

constexpr int test_two_members() {
    Point p(10, 20);
    return p.x + p.y;
}

static_assert(test_two_members() == 30, "Should return 30");

int main() {
    return 0;
}
