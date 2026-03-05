// Test constinit with struct aggregate initialization and array initialization
// Item #8 fix: InitializerListNode elements are validated individually for constinit

struct Point {
    int x;
    int y;
};

constinit int arr[] = {10, 12, 20};  // Valid constinit array init

constinit Point p = {10, 22};  // Valid constinit struct aggregate init

int main() {
    return arr[0] + p.x + p.y;  // 10 + 10 + 22 = 42
}
