struct Point {
    int x;
    int y;
    Point(int x_val, int y_val) : x(x_val), y(y_val) {}
};

int main() {
    Point p1(10, 20);  // Local variable with constructor
    return p1.x;
}
