// Test constexpr struct member access when struct is aggregate-initialized
// Item #8 fix: evaluate_member_access now handles InitializerListNode initializers

struct Point {
    int x;
    int y;
};

constexpr Point origin = {10, 32};  // Aggregate init (no constructor call)

constexpr int sum_xy = origin.x + origin.y;  // Should evaluate to 42

int main() {
    return sum_xy;
}
