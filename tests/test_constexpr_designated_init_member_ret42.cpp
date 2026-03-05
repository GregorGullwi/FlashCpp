// Test Bug 1 fix: pos_idx must only advance for positional (non-designated) initializers.
// A constexpr struct with a designated initializer must allow accessing the designated member.

struct Vec {
    int x = 0;
    int y = 0;
    int z = 0;
};

constexpr Vec v = {.y = 42};

constexpr int val = v.y;  // Must be 42

int main() {
    return val;
}
