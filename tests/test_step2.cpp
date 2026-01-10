struct Point {
    int x;
};

int main() {
    Point p = {10};
    int Point::*ptr = nullptr;
    return 0;
}
