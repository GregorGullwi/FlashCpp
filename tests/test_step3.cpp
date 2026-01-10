struct Point {
    int x;
};

int main() {
    Point p = {10};
    int Point::*ptr = nullptr;
    p.*ptr;
    return 0;
}
