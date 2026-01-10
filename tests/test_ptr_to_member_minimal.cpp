// Minimal test for pointer-to-member declaration parsing
struct Point {
    int x;
};

int main() {
    int Point::*ptr;
    return 0;
}
