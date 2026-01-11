// Test pointer-to-member without taking address
struct Point {
    int x;
};

int main() {
    int Point::*ptr = nullptr;
    return 0;
}
