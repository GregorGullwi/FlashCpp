// Test address-of member
struct Point {
    int x;
};

int main() {
    // Try to take address of member
    int Point::*ptr = &Point::x;
    return 0;
}
