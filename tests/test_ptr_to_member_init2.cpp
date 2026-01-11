// Test pointer-to-member without taking address
struct Point {
    int x;
};

int Point::*getPtr() {
    return nullptr;
}

int main() {
    int Point::*ptr = getPtr();
    return 0;
}
