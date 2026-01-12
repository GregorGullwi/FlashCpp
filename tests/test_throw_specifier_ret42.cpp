// Test throw() specifier on constructors/destructors (pre-C++17 style)
// This pattern is used extensively in standard library headers like <new>

class TestException {
public:
    TestException() throw() {}
    ~TestException() throw() {}
    virtual const char* what() const throw() { return "test"; }
};

// Also test noexcept style for comparison
class ModernException {
public:
    ModernException() noexcept {}
    ~ModernException() noexcept {}
    virtual const char* what() const noexcept { return "modern"; }
};

int main() {
    TestException e;
    ModernException m;
    return 42;
}
