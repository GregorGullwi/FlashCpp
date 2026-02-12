// Test: constexpr/inline/static specifiers work in any order for struct members
struct Foo {
    static constexpr int x = 10;           // canonical order
    constexpr static int y = 20;           // reversed order
    inline static constexpr int z = 30;    // inline first
};

int main() {
    return Foo::x + Foo::y + Foo::z - 60;  // should return 0
}
