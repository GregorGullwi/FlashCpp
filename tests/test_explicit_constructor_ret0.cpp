// Test explicit constructor keyword
// This pattern is commonly used in standard library headers

class value_wrapper {
public:
    explicit value_wrapper(int x) : value_(x) { }
    
    int get() const { return value_; }
    
private:
    int value_;
};

// Test explicit with constexpr
class constexpr_wrapper {
public:
    explicit constexpr constexpr_wrapper(int x) noexcept : value_(x) { }
    
    constexpr int get() const noexcept { return value_; }
    
private:
    int value_;
};

int main() {
    value_wrapper v(42);
    constexpr_wrapper cw(10);
    return v.get() - cw.get() - 32;  // Should return 0
}
