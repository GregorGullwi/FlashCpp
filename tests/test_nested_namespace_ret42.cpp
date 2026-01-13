// Test C++17 nested namespace declarations
// Pattern from bits/version.h: namespace ranges::__detail { }

// Nested namespace with struct
namespace A::B {
    struct Data {
        int x;
        int y;
    };
}

int main() {
    A::B::Data d;
    d.x = 42;
    d.y = 0;
    return d.x;
}
