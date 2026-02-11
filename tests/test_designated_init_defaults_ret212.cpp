// Test: designated initializers with default member values for omitted fields
// C++20: omitted fields in designated init should use default member initializer

struct Config {
    int width = 80;
    int height = 60;
    int depth = 32;
};

int main() {
    // Only specify height, width and depth should use their defaults
    Config c = {.height = 100};
    // width=80, height=100, depth=32 → 80 + 100 + 32 = 212
    return c.width + c.height + c.depth;
}
