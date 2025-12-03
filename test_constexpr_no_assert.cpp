struct Point {
    int x;
    int y;
    constexpr Point(int x_val, int y_val) : x(x_val), y(y_val) {}
    constexpr int sum() const {
        return x + y;
    }
};

constexpr Point p1(10, 20);

int main() {
    Point p2(5, 10);
    return 0;
}
