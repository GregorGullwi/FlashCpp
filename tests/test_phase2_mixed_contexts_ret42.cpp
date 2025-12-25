// Phase 2: Test qualified identifiers with mixed template and non-template contexts

namespace lib {
    namespace utils {
        // Non-template function
        int getValue() {
            return 10;
        }
        
        // Template function
        template<typename T>
        T multiply(T x, int factor) {
            return x * factor;
        }
    }
}

int main() {
    // Test 1: Non-template qualified call
    int val1 = lib::utils::getValue();
    
    // Test 2: Template qualified call
    int val2 = lib::utils::multiply<int>(5, 2);
    
    // Test 3: Mix both
    int val3 = lib::utils::getValue() + lib::utils::multiply<int>(3, 4);
    
    return val1 + val2 + val3;  // 10 + 10 + 22 = 42
}
