// Test RVO with mixed int and float struct
// This tests proper handling of different register types

extern "C" int printf(const char*, ...);

static int copy_count = 0;
static int ctor_count = 0;

struct MixedStruct {
    int i1;
    float f1;
    int i2;
    double d1;
    float f2;
    int i3;
    
    // Regular constructor
    MixedStruct(int i1_val, float f1_val, int i2_val, double d1_val, float f2_val, int i3_val)
        : i1(i1_val), f1(f1_val), i2(i2_val), d1(d1_val), f2(f2_val), i3(i3_val) {
        ctor_count++;
        printf("MixedStruct constructor called (count=%d)\n", ctor_count);
    }
    
    // Copy constructor
    MixedStruct(const MixedStruct& other)
        : i1(other.i1), f1(other.f1), i2(other.i2), d1(other.d1), f2(other.f2), i3(other.i3) {
        copy_count++;
        printf("MixedStruct copy constructor called (count=%d)\n", copy_count);
    }
};

// RVO with mixed int/float struct
MixedStruct makeMixedStruct() {
    return MixedStruct(1, 2.5f, 3, 4.75, 5.25f, 6);
}

int main() {
    printf("=== Testing RVO with Mixed Int/Float Struct ===\n");
    MixedStruct ms = makeMixedStruct();
    
    printf("\nResults:\n");
    printf("  Constructors: %d (expected: 1)\n", ctor_count);
    printf("  Copies: %d (expected: 0 with RVO)\n", copy_count);
    printf("  Values: i1=%d, f1=%f, i2=%d, d1=%f, f2=%f, i3=%d\n",
           ms.i1, ms.f1, ms.i2, ms.d1, ms.f2, ms.i3);
    
    // Check values
    int values_ok = (ms.i1 == 1 && ms.i2 == 3 && ms.i3 == 6);
    
    // Success if: exactly 1 constructor call, 0 copies, correct values
    if (ctor_count == 1 && copy_count == 0 && values_ok) {
        printf("\nMIXED STRUCT RVO TEST PASSED\n");
        return 0;
    } else {
        printf("\nMIXED STRUCT RVO TEST FAILED\n");
        return 1;
    }
}
