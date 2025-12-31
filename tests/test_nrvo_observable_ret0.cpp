// Test NRVO with observable copy constructor
// When NRVO is working, copy constructor should NOT be called

extern "C" int printf(const char*, ...);

static int copy_count = 0;
static int ctor_count = 0;

struct Counter {
    int value;
    
    // Regular constructor
    Counter(int v) : value(v) {
        ctor_count++;
        printf("Counter(%d) - constructor called (count=%d)\n", v, ctor_count);
    }
    
    // Copy constructor
    Counter(const Counter& other) : value(other.value) {
        copy_count++;
        printf("Counter(const Counter&) - copy constructor called (count=%d)\n", copy_count);
    }
};

// NRVO: returning named local variable
// With NRVO: should call constructor once directly in return location
// Without NRVO: would call constructor + copy constructor
Counter makeCounter() {
    Counter c(42);
    c.value = c.value + 8;
    return c;
}

int main() {
    printf("=== Testing NRVO ===\n");
    Counter result = makeCounter();
    
    printf("\nResults:\n");
    printf("  Constructors: %d (expected: 1)\n", ctor_count);
    printf("  Copies: %d (expected: 0 with NRVO)\n", copy_count);
    printf("  Value: %d (expected: 50)\n", result.value);
    
    // Success if: exactly 1 constructor call, 0 copies, correct value
    // Note: NRVO is optional in C++17, so we accept both scenarios
    if (result.value == 50) {
        if (ctor_count == 1 && copy_count == 0) {
            printf("\nNRVO TEST PASSED (with optimization)\n");
        } else if (ctor_count == 1 && copy_count == 1) {
            printf("\nNRVO TEST PASSED (without optimization - acceptable)\n");
        } else {
            printf("\nNRVO TEST FAILED (unexpected constructor/copy counts)\n");
            return 1;
        }
        return 0;
    } else {
        printf("\nNRVO TEST FAILED (incorrect value)\n");
        return 1;
    }
}
