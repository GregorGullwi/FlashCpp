// Test switch statements with various patterns

// Test 1: Basic switch with break
int test_basic_switch(int x) {
    int result = 0;
    switch (x) {
        case 1:
            result = 10;
            break;
        case 2:
            result = 20;
            break;
        default:
            result = 99;
            break;
    }
    return result;
}

// Test 2: Switch with fall-through
int test_fallthrough(int x) {
    int result = 0;
    switch (x) {
        case 1:
        case 2:
            result = 12;
            break;
        default:
            result = 0;
            break;
    }
    return result;
}

