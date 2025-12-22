// Test for [*this] capture with mutation inside lambda
// This verifies that [*this] truly creates a copy of the object
// Mutations inside the lambda should NOT affect the original object

struct Counter {
    int value = 10;
    
    int test_mutation_does_not_affect_original() {
        // [*this] captures a copy, so mutations inside lambda shouldn't affect original
        auto lambda = [*this]() mutable {
            value = 99;  // Modify the COPY (implicit member access)
            return value;
        };
        
        int lambda_result = lambda();  // Should return 99 (modified copy)
        // Original value should still be 10
        return value + lambda_result;  // Should be 10 + 99 = 109
    }
    
    int test_multiple_mutations() {
        auto lambda = [*this]() mutable {
            value += 5;
            value *= 2;
            return value;
        };
        
        int lambda_result = lambda();  // Should return (10+5)*2 = 30
        // Original value should still be 10
        return value + lambda_result;  // Should be 10 + 30 = 40
    }
    
    int test_verify_original_unchanged() {
        int original_value = value;  // Save original (10)
        
        auto lambda = [*this]() mutable {
            value = 777;  // Modify copy
            return value;
        };
        
        lambda();  // Call it
        
        // Verify original is unchanged
        if (value == original_value) {
            return 100;  // Success
        }
        return 0;  // Failure
    }
};

int main() {
    Counter c;
    int result = 0;
    
    result += c.test_mutation_does_not_affect_original();  // 109
    c.value = 10;  // Reset
    result += c.test_multiple_mutations();                  // 40
    c.value = 10;  // Reset
    result += c.test_verify_original_unchanged();           // 100
    
    return result;  // Expected: 109 + 40 + 100 = 249
}
