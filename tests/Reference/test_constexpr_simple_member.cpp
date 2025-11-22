// Simple constexpr member function test

struct Point {
    int x;
    int y;

    constexpr Point(int x_val, int y_val) : x(x_val), y(y_val) {}

    constexpr int get_x() {
        return x;
    }
};

constexpr Point p1(10, 20);
static_assert(p1.get_x() == 10, "p1.get_x() should be 10");

int main() {
    return 0;
}
