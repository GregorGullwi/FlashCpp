// Test __builtin_expect intrinsic
// This is a branch prediction hint

long __builtin_expect(long, long);

int main() {
    int x = 42;
    
    // __builtin_expect(expr, expected) returns expr
    // but hints that expr will likely equal expected
    if (__builtin_expect(x == 42, 1)) {  // likely true
        return 42;
    }
    
    return 0;  // Expected: 42
}
