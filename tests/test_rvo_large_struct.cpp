// Test RVO with larger struct (multiple fields, both int and float)
// This tests multi-register parameter passing and struct returns

extern "C" int printf(const char*, ...);

static int copy_count = 0;
static int ctor_count = 0;

struct LargeStruct {
    int a;
    int b;
    float c;
    double d;
    int e;
    float f;
    
    // Regular constructor
    LargeStruct(int a_val, int b_val, float c_val, double d_val, int e_val, float f_val)
        : a(a_val), b(b_val), c(c_val), d(d_val), e(e_val), f(f_val) {
        ctor_count++;
        printf("LargeStruct constructor called (count=%d)\n", ctor_count);
    }
    
    // Copy constructor
    LargeStruct(const LargeStruct& other)
        : a(other.a), b(other.b), c(other.c), d(other.d), e(other.e), f(other.f) {
        copy_count++;
        printf("LargeStruct copy constructor called (count=%d)\n", copy_count);
    }
};

// RVO with large struct (32 bytes total)
LargeStruct makeLargeStruct() {
    return LargeStruct(10, 20, 3.14f, 2.71828, 42, 1.618f);
}

int main() {
    printf("=== Testing RVO with Large Struct ===\n");
    LargeStruct ls = makeLargeStruct();
    
    printf("\nResults:\n");
    printf("  Constructors: %d (expected: 1)\n", ctor_count);
    printf("  Copies: %d (expected: 0 with RVO)\n", copy_count);
    printf("  Values: a=%d, b=%d, c=%f, d=%f, e=%d, f=%f\n", 
           ls.a, ls.b, ls.c, ls.d, ls.e, ls.f);
    
    // Check values
    int values_ok = (ls.a == 10 && ls.b == 20 && ls.e == 42);
    
    // Success if: exactly 1 constructor call, 0 copies, correct values
    if (ctor_count == 1 && copy_count == 0 && values_ok) {
        printf("\nLARGE STRUCT RVO TEST PASSED\n");
        return 0;
    } else {
        printf("\nLARGE STRUCT RVO TEST FAILED\n");
        return 1;
    }
}
