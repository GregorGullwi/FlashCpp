// Test pointer-to-member initialization
struct Point {
    int x;
};

int main() {
    int Point::*ptr = &Point::x;
    return 0;
}
