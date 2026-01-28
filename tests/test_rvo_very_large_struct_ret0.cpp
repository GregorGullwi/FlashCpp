// Test RVO with very large struct that requires stack passing
// This tests stack-passed parameters and large struct returns

extern "C" int printf(const char*, ...);

static int copy_count = 0;
static int ctor_count = 0;

struct VeryLargeStruct {
    int values[20];  // 80 bytes total - definitely on stack
    float floats[10]; // 40 more bytes = 120 bytes total
    
    // Regular constructor
    VeryLargeStruct(int start_val) {
        ctor_count++;
        printf("VeryLargeStruct constructor called (count=%d)\n", ctor_count);
        for (int i = 0; i < 20; i++) {
            values[i] = start_val + i;
        }
        for (int j = 0; j < 10; j++) {
            floats[j] = static_cast<float>(start_val + j) * 1.5f;
        }
    }
    
    // Copy constructor
    VeryLargeStruct(const VeryLargeStruct& other) {
        copy_count++;
        printf("VeryLargeStruct copy constructor called (count=%d)\n", copy_count);
        for (int i = 0; i < 20; i++) {
            values[i] = other.values[i];
        }
        for (int j = 0; j < 10; j++) {
            floats[j] = other.floats[j];
        }
    }
};

// RVO with very large struct (120 bytes)
VeryLargeStruct makeVeryLargeStruct() {
    return VeryLargeStruct(100);
}

int main() {
    printf("=== Testing RVO with Very Large Struct (Stack-Passed) ===\n");
    VeryLargeStruct vls = makeVeryLargeStruct();
    
    printf("\nResults:\n");
    printf("  Constructors: %d (expected: 1)\n", ctor_count);
    printf("  Copies: %d (expected: 0 with RVO)\n", copy_count);
    printf("  First value: %d (expected: 100)\n", vls.values[0]);
    printf("  Last value: %d (expected: 119)\n", vls.values[19]);
    printf("  First float: %f (expected: 150.0)\n", vls.floats[0]);
    
    // Check values
    int values_ok = (vls.values[0] == 100 && vls.values[19] == 119);
    
    // Success if: exactly 1 constructor call, 0 copies, correct values
    if (ctor_count == 1 && copy_count == 0 && values_ok) {
        printf("\nVERY LARGE STRUCT RVO TEST PASSED\n");
        return 0;
    } else {
        printf("\nVERY LARGE STRUCT RVO TEST FAILED\n");
        return 1;
    }
}
