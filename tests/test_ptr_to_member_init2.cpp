// Test pointer-to-member without taking address
struct Point {
    int x;
};

int Point::*getPtr();

int main() {
    int Point::*ptr = getPtr();
    return 0;
}
