// Test that constinit with non-constant struct member fails
// Item #8 fix: constinit struct aggregate init must be compile-time constant

int runtime_val = 5;

struct Point {
int x;
int y;
};

constinit Point p = {runtime_val, 2};  // ERROR: runtime_val is not constexpr

int main() {
return p.x;
}
