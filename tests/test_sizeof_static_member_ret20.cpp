// Test: sizeof on static and non-static members via qualified-id syntax
// Verifies sizeof(Struct::member) works for static int, static pointer, static reference, and non-static members.

int x = 10;

struct Sizes {
    static int val;
    static int* ptr;
    static int& ref;
    int member;
};

int Sizes::val = 42;
int* Sizes::ptr = &x;
int& Sizes::ref = x;

int main() {
    // sizeof(Sizes::val) = 4, sizeof(Sizes::ptr) = 8, sizeof(Sizes::ref) = 4 (referenced type), sizeof(Sizes::member) = 4
    return sizeof(Sizes::val) + sizeof(Sizes::ptr) + sizeof(Sizes::ref) + sizeof(Sizes::member);  // 20
}
