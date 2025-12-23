// Test __builtin_unreachable intrinsic
// This tells the compiler a code path is never reached

void __builtin_unreachable();

int test_switch(int x) {
    switch(x) {
        case 1: return 10;
        case 2: return 20;
        default: __builtin_unreachable();
    }
    // Control should never reach here
}

int main() {
    return test_switch(1);  // Expected: 10
}
