// Test static member inheritance through base classes
// This tests Priority 3: Template Specialization Inheritance

// Simple non-template case
struct Base {
    static constexpr int base_value = 100;
};

struct Derived : Base {
    // Inherits base_value from Base
};

int main() {
    // Test: Simple inheritance - access via instance
    Derived d;
    return d.base_value - 100;  // Should return 0 if inheritance works
}
