// Verify it's valid C++20
namespace calc {
    int add(int a, int b) noexcept;
    
    int compute(int x) noexcept {
        int add(int a, int b) noexcept;  // block-scope redeclaration
        return add(x, 1);  // should call calc::add, not ::add
    }
    
    int add(int a, int b) noexcept {
        return a + b;
    }
}

int main() {
    return calc::compute(41);  // should return 42
}
