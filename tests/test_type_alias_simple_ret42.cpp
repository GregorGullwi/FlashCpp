// Simple test for type alias resolution in expression context

template<bool B>
struct bool_constant {
    static constexpr bool value = B;
};

using true_type = bool_constant<true>;
using false_type = bool_constant<false>;

int main() {
    // This should compile without "Missing identifier" error
    bool b1 = true_type::value;   // Should be true
    bool b2 = false_type::value;  // Should be false
    
    return b1 ? 42 : 0;  // Should return 42
}
