// Test for __is_aggregate compiler intrinsic
// Tests basic aggregate detection

struct SimpleAggregate {
    int x;
    double y;
};

struct WithConstructor {
    int x;
    WithConstructor() : x(0) { }
};

int main() {
    // All results together - if all correct, should be 2 (two true results)
    int result = 0;
    
    // SimpleAggregate should be aggregate
    result += __is_aggregate(SimpleAggregate) ? 1 : 0;
    
    // WithConstructor should NOT be aggregate  
    result += __is_aggregate(WithConstructor) ? 0 : 0;
    
    // int should NOT be aggregate
    result += __is_aggregate(int) ? 0 : 0;
    
    // int[5] should be aggregate
    result += __is_aggregate(int[5]) ? 1 : 0;
    
    // If all tests pass, result should be 2
    return result == 2 ? 0 : 1;
}
