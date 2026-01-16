// Test for namespace class member function call mangling fix
// This verifies that member function calls on classes defined in namespaces
// are correctly mangled and linked.

namespace ns {
    class Test {
    public:
        int value;
        int get_value() const { return value; }
    };
}

int main() {
    ns::Test t;
    t.value = 42;
    return t.get_value();  // Should return 42
}
