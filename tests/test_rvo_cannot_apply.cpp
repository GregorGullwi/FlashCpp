// Test cases where RVO/NRVO CANNOT be applied according to C++17 standard
// These should compile and run correctly, just without copy elision

extern "C" int printf(const char*, ...);

static int ctor_count = 0;
static int copy_count = 0;

struct Value {
    int x;
    
    Value(int v) : x(v) {
        ctor_count++;
        printf("Value(%d) - constructor (count=%d)\n", v, ctor_count);
    }
    
    Value(const Value& other) : x(other.x) {
        copy_count++;
        printf("Value(const Value&) - copy constructor (count=%d)\n", copy_count);
    }
};

// Case 1: Multiple return paths with different objects
// RVO cannot be applied - different objects returned on different paths
Value multipleReturns(bool condition) {
    printf("\n=== Test 1: Multiple return paths ===\n");
    if (condition) {
        Value v1(10);
        return v1;  // Cannot apply RVO - multiple return paths
    } else {
        Value v2(20);
        return v2;  // Cannot apply RVO - multiple return paths
    }
}

// Case 2: Returning a parameter
// RVO cannot be applied - parameter lives in caller's frame
Value returningParameter(Value param) {
    printf("\n=== Test 2: Returning parameter ===\n");
    return param;  // Cannot apply RVO - returning parameter
}

// Case 3: Conditional return with same variable
// NRVO MAY be applied - same named variable on all paths
Value conditionalSameVariable(bool condition) {
    printf("\n=== Test 3: Conditional with same variable ===\n");
    Value v(30);
    if (condition) {
        v.x += 5;
    } else {
        v.x += 10;
    }
    return v;  // NRVO may be applied - single variable
}

// Case 4: Return with explicit std::move
// This would prevent copy elision and force move
namespace std {
    template<typename T> struct remove_reference      { using type = T; };
    template<typename T> struct remove_reference<T&>  { using type = T; };
    template<typename T> struct remove_reference<T&&> { using type = T; };
    
    template<typename T>
    typename remove_reference<T>::type&& move(T&& arg) {
        using ReturnType = typename remove_reference<T>::type&&;
        return static_cast<ReturnType>(arg);
    }
}

Value explicitMove() {
    printf("\n=== Test 4: Explicit std::move ===\n");
    Value v(40);
    return std::move(v);  // Explicit move prevents NRVO
}

int main() {
    printf("=== Testing RVO/NRVO edge cases ===\n");
    
    // Reset counters
    ctor_count = 0;
    copy_count = 0;
    
    // Test 1: Multiple returns
    Value r1 = multipleReturns(true);
    printf("Result: x=%d (constructors=%d, copies=%d)\n", r1.x, ctor_count, copy_count);
    int test1_ctors = ctor_count;
    int test1_copies = copy_count;
    
    // Reset counters
    ctor_count = 0;
    copy_count = 0;
    
    // Test 2: Returning parameter
    Value param(50);
    Value r2 = returningParameter(param);
    printf("Result: x=%d (constructors=%d, copies=%d)\n", r2.x, ctor_count, copy_count);
    int test2_ctors = ctor_count;
    int test2_copies = copy_count;
    
    // Reset counters
    ctor_count = 0;
    copy_count = 0;
    
    // Test 3: Conditional same variable
    Value r3 = conditionalSameVariable(true);
    printf("Result: x=%d (constructors=%d, copies=%d)\n", r3.x, ctor_count, copy_count);
    int test3_ctors = ctor_count;
    int test3_copies = copy_count;
    
    // Reset counters
    ctor_count = 0;
    copy_count = 0;
    
    // Test 4: Explicit std::move
    Value r4 = explicitMove();
    printf("Result: x=%d (constructors=%d, copies=%d)\n", r4.x, ctor_count, copy_count);
    int test4_ctors = ctor_count;
    int test4_copies = copy_count;
    
    printf("\n=== Summary ===\n");
    printf("Test 1 (multiple returns): constructors=%d, copies=%d\n", test1_ctors, test1_copies);
    printf("Test 2 (returning parameter): constructors=%d, copies=%d\n", test2_ctors, test2_copies);
    printf("Test 3 (conditional same var): constructors=%d, copies=%d\n", test3_ctors, test3_copies);
    printf("Test 4 (explicit std::move): constructors=%d, copies=%d\n", test4_ctors, test4_copies);
    
    // Validate results
    if (r1.x == 10 && r2.x == 50 && r3.x == 35 && r4.x == 40) {
        printf("\nAll values correct!\n");
        printf("TEST PASSED\n");
        return 0;
    } else {
        printf("\nERROR: Incorrect values (r1.x=%d expected 10, r2.x=%d expected 50, r3.x=%d expected 35, r4.x=%d expected 40)\n", 
               r1.x, r2.x, r3.x, r4.x);
        return 1;
    }
}
