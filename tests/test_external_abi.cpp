// Test ABI compatibility by calling external functions compiled with standard compiler
// This verifies that FlashCpp's calling convention matches the platform ABI

// Declare external functions from test_external_abi_helper.c
extern "C" int external_int_6_params(int a, int b, int c, int d, int e, int f);
extern "C" double external_mixed_params(int a, double b, int c, double d);
extern "C" int external_many_params(int p1, int p2, int p3, int p4, int p5, int p6,
                                    int p7, int p8, int p9, int p10);
extern "C" double external_mixed_stack(int i1, double d1, int i2, double d2, int i3, double d3,
                                       int i4, double d4, int i5, double d5);
struct Big3 { int a; int b; int c; };
extern "C" int external_sum_big3(Big3 value);
extern "C" int external_big3_after_4_ints(int i1, int i2, int i3, int i4, Big3 value);
extern "C" int external_big3_after_5_ints(int i1, int i2, int i3, int i4, int i5, Big3 value);

extern "C" int main() {
    int result = 0;
    
    // Test 1: 6 integer parameters (all in registers on Linux: RDI, RSI, RDX, RCX, R8, R9)
    int test1 = external_int_6_params(1, 2, 3, 4, 5, 6);
    if (test1 != 21) {
        return 1; // Expected: 1+2+3+4+5+6 = 21
    }
    
    // Test 2: Mixed int/double parameters (separate register pools)
    // On Linux: a→RDI, b→XMM0, c→RSI, d→XMM1
    double test2 = external_mixed_params(10, 20.5, 30, 40.5);
    if (test2 < 100.9 || test2 > 101.1) {
        return 2; // Expected: 10+20.5+30+40.5 = 101.0
    }
    
    // Test 3: Many integer parameters (tests stack passing)
    // On Linux: p1-p6 in registers (RDI,RSI,RDX,RCX,R8,R9), p7-p10 on stack
    int test3 = external_many_params(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    if (test3 != 55) {
        return 3; // Expected: sum(1..10) = 55
    }
    
    // Test 4: Mixed types with stack overflow (tests both register pools and stack)
    // On Linux: i1-i6 use RDI,RSI,RDX,RCX,R8,R9; d1-d5 use XMM0-XMM4
    // But we only have 6 int regs and 8 float regs, so some should go on stack
    double test4 = external_mixed_stack(1, 2.5, 3, 4.5, 5, 6.5, 7, 8.5, 9, 10.5);
    if (test4 < 57.4 || test4 > 57.6) {
        return 4; // Expected: 1+2.5+3+4.5+5+6.5+7+8.5+9+10.5 = 57.5
    }

    // Test 5: 12-byte struct parameter (must use two-register SysV ABI, not pointer convention)
    Big3 test5 = {10, 12, 20};
    if (external_sum_big3(test5) != 42) {
        return 5;
    }

    // Test 6: Big3 after 4 ints — Big3 still fits in registers (R8+R9)
    // i1→RDI, i2→RSI, i3→RDX, i4→RCX, Big3 low→R8, Big3 high→R9
    Big3 test6 = {100, 200, 300};
    if (external_big3_after_4_ints(1, 2, 3, 4, test6) != 610) {
        return 6; // Expected: 1+2+3+4+100+200+300 = 610
    }

    // Test 7: Big3 after 5 ints — Big3 overflows to the stack
    // i1→RDI, i2→RSI, i3→RDX, i4→RCX, i5→R8 (5 regs used, only 1 left, Big3 needs 2 → stack)
    Big3 test7 = {100, 200, 300};
    if (external_big3_after_5_ints(1, 2, 3, 4, 5, test7) != 615) {
        return 7; // Expected: 1+2+3+4+5+100+200+300 = 615
    }
    
    return 0; // All tests passed!
}
