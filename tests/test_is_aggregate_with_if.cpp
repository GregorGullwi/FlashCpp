// Test for __is_aggregate with direct if statements
// Tests if bool results from intrinsics work correctly in conditionals

struct SimpleAggregate {
    int x;
    double y;
};

struct WithConstructor {
    int x;
    WithConstructor() : x(0) { }
};

int main() {
    // Test 1: SimpleAggregate should be aggregate
    if (!__is_aggregate(SimpleAggregate)) {
        return 1; // FAIL - should be aggregate
    }
    
    // Test 2: WithConstructor should NOT be aggregate
    if (__is_aggregate(WithConstructor)) {
        return 2; // FAIL - should NOT be aggregate
    }
    
    // Test 3: int should NOT be aggregate
    if (__is_aggregate(int)) {
        return 3; // FAIL - should NOT be aggregate
    }
    
    // Test 4: int[5] should be aggregate
    if (!__is_aggregate(int[5])) {
        return 4; // FAIL - should be aggregate
    }
    
    // All tests passed
    return 0;
}
