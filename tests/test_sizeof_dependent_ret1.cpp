// Test: static_assert with sizeof on dependent types in templates
// sizeof of dependent types should be deferred, not evaluated as 0
// This pattern is used in libstdc++ atomic_wait.h

template<typename T>
struct SizeChecker {
    using value_type = T;
    
    // static_assert that depends on template parameter via type alias
    // Should be deferred during template definition
    static_assert(sizeof(value_type) > 0, "size must be positive");
};

int main() {
    return sizeof(SizeChecker<char>::value_type);
}
