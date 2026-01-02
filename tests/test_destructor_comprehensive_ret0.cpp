// Comprehensive test for explicit destructor calls
// Tests:
// 1. Explicit destructor call on user-defined types
// 2. Explicit destructor call in template context with user-defined types
// 3. Pseudo-destructor call on native types in template context (no-op)

// Counter to track destructor calls
int destructor_count = 0;

struct Counter {
    int value;
    
    Counter(int v = 0) : value(v) {}
    
    ~Counter() {
        destructor_count++;
    }
};

// Template function that explicitly calls destructor
// Works for both user-defined types and native types
template<typename T>
void destroy_object(T& obj) {
    obj.~T();  // For user-defined types: calls actual destructor
               // For native types: pseudo-destructor call (no-op)
}

int main() {
    // Test 1: Explicit destructor call on user-defined type (non-template)
    destructor_count = 0;
    {
        Counter c1(42);
        
        // Manually call destructor
        c1.~Counter();
        
        // Counter should be 1 after explicit call
        if (destructor_count != 1) {
            return 1;  // Failed - destructor not called
        }
    }
    // Automatic destructor call at end of scope (count becomes 2)
    // Reset for next test
    
    // Test 2: Explicit destructor call via template function with user-defined type
    destructor_count = 0;
    {
        Counter c2(100);
        
        // Explicitly destroy via template function
        destroy_object(c2);
        
        // Check destructor was called
        if (destructor_count != 1) {
            return 2;  // Failed - template destructor not called
        }
    }
    // Automatic destructor call at end of scope (count becomes 2)
    
    // Test 3: Pseudo-destructor call on native type in template (should be no-op)
    {
        int x = 42;
        
        // This should compile and be a no-op (pseudo-destructor)
        // When T=int, obj.~T() becomes a pseudo-destructor call
        destroy_object(x);
        
        // Should still have the same value
        if (x != 42) {
            return 3;  // Failed - value changed
        }
    }
    
    // Test 4: Pseudo-destructor on other native types in template
    {
        float f = 3.14f;
        destroy_object(f);
        
        // Value should be unchanged (comparing with small epsilon for float)
        if (f < 3.13f || f > 3.15f) {
            return 4;  // Failed - float value changed
        }
        
        long long ll = 1234567890LL;
        destroy_object(ll);
        
        if (ll != 1234567890LL) {
            return 5;  // Failed - long long value changed
        }
    }
    
    // All tests passed
    return 0;
}
