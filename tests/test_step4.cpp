struct Point {
    int x;
};

int main() {
    Point p = {10};
    int Point::*abc = nullptr;
    p.*abc;
    return 0;
}
