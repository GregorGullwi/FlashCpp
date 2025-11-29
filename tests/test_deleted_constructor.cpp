// Test = delete syntax for constructors
struct NonCopyable {
    int x;
    
    // Default constructor is allowed
    NonCopyable() = default;
    
    // Delete copy constructor
    NonCopyable(NonCopyable& other) = delete;
    
    // Delete copy assignment operator
    NonCopyable& operator=(NonCopyable& other) = delete;
};

int main() {
    return 0;
}

