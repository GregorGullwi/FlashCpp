// Test C++17 nested namespace declarations
// Pattern from bits/version.h: namespace ranges::__detail { }

// Regular nested namespace with struct
namespace A::B {
    struct Data {
        int x;
        int y;
    };
}

// C++20 nested namespace with inline keyword
// Pattern: namespace X::inline Y { }
// The inline keyword makes Y's members visible in X
namespace X::inline Y {
    struct Point {
        int a;
        int b;
    };
}

int main() {
    // Regular nested namespace - requires full qualification
    A::B::Data d;
    d.x = 42;
    d.y = 0;
    
    // Inline nested namespace - can access with X:: OR X::Y:: prefix
    // Because Y is inline, both X::Point and X::Y::Point refer to the same type
    X::Point p;
    p.a = d.x;
    p.b = 0;
    
    return p.a;  // Should return 42
}
