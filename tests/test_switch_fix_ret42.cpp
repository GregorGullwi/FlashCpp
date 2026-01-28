
// Test explicitly for switch statement logic which was previously inverted
// Expected return: 42

int test_simple_switch(int val) {
    switch (val) {
        case 1: return 10;
        case 2: return 20;
        case 3: return 30;
        default: return 0;
    }
}

int test_fallthrough(int val) {
    int res = 0;
    switch (val) {
        case 1: res += 1;
        case 2: res += 2;
        case 3: res += 3; break;
        case 4: res += 4;
    }
    return res;
}

int main() {
    int total = 0;

    // 1. Test Match (should execute body)
    // Bug check: If logic is inverted, test_simple_switch(2) might skip Executing case 2
    if (test_simple_switch(1) == 10) total += 1;
    if (test_simple_switch(2) == 20) total += 1;
    if (test_simple_switch(3) == 30) total += 1;
    if (test_simple_switch(99) == 0) total += 1;

    // 2. Test Mismatch (should NOT execute body)
    // If logic is inverted, passing 99 would execute the first case it checked (case 1)
    
    // 3. Test Fallthrough
    // val=1 -> case 1 (1) + case 2 (2) + case 3 (3) = 6
    if (test_fallthrough(1) == 6) total += 10;
    
    // val=2 -> case 2 (2) + case 3 (3) = 5
    if (test_fallthrough(2) == 5) total += 10;

    // Expected total: 1+1+1+1 + 10+10 = 24
    // We want to return 42, so add 18
    return total + 18;
}
