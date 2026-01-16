// Test for struct constructor with std::initializer_list-like structure  
// Tests that std:: namespace is properly mangled with "St" substitution

// Simplified std::initializer_list class (non-template version for testing)
namespace std {
    class simple_list {
    public:
        int value_;
        
        simple_list() : value_(0) {}
        simple_list(int v) : value_(v) {}
        
        int get() const { return value_; }
    };
}

int main() {
    std::simple_list list(42);
    
    // Return the value - should be 42
    return list.get();
}
