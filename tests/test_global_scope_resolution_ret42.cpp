// Test: ::ns::identifier resolves from global namespace, not relative
namespace ns {
    int get_value() { return 42; }
    
    namespace ns {
        int get_value() { return 10; }
    }
    
    int test() {
        // ::ns::get_value() must resolve to global ns (42), not ns::ns (10)
        return ::ns::get_value();
    }
}

int main() {
    return ns::test();
}
