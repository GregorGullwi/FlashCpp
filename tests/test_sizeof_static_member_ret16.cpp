// Test: sizeof on static and non-static members via qualified-id syntax
// Verifies sizeof(Struct::member) works for static int, static pointer, and non-static members.

struct Sizes {
    static int val;
    static int* ptr;
    int member;
};

int main() {
    // sizeof(Sizes::val) = 4, sizeof(Sizes::ptr) = 8, sizeof(Sizes::member) = 4
    return sizeof(Sizes::val) + sizeof(Sizes::ptr) + sizeof(Sizes::member);  // 16
}
