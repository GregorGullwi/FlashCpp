// Test nested loops with break and continue to ensure they target the correct loop

int test_nested_while_break_inner() {
    // Break should only exit the inner loop, not the outer loop
    int sum = 0;
    int i = 0;
    while (i < 3) {
        int j = 0;
        while (j < 5) {
            if (j >= 2) {
                break;  // Should break inner loop only
            }
            sum = sum + (i * 10) + j;
            j++;
        }
        i++;
    }
    // Should execute: i=0,j=0,1  i=1,j=0,1  i=2,j=0,1
    // sum = 0+1 + 10+11 + 20+21 = 1 + 21 + 41 = 63
    return sum;
}

int test_nested_while_continue_inner() {
    // Continue should only skip to next iteration of inner loop
    int sum = 0;
    int i = 0;
    while (i < 3) {
        int j = 0;
        while (j < 5) {
            j++;
            if (j == 2) {
                continue;  // Should continue inner loop only
            }
            sum = sum + (i * 10) + j;
        }
        i++;
    }
    // Should execute: i=0,j=1,3,4,5  i=1,j=1,3,4,5  i=2,j=1,3,4,5
    // sum = (1+3+4+5) + (11+13+14+15) + (21+23+24+25) = 13 + 53 + 93 = 159
    return sum;
}

int test_nested_for_break_inner() {
    // Break in inner for loop
    int sum = 0;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 5; j++) {
            if (j >= 2) {
                break;  // Should break inner loop only
            }
            sum = sum + (i * 10) + j;
        }
    }
    // Should execute: i=0,j=0,1  i=1,j=0,1  i=2,j=0,1
    // sum = 0+1 + 10+11 + 20+21 = 1 + 21 + 41 = 63
    return sum;
}

int test_nested_for_continue_inner() {
    // Continue in inner for loop
    int sum = 0;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 5; j++) {
            if (j == 2) {
                continue;  // Should continue inner loop only
            }
            sum = sum + (i * 10) + j;
        }
    }
    // Should execute: i=0,j=0,1,3,4  i=1,j=0,1,3,4  i=2,j=0,1,3,4
    // sum = (0+1+3+4) + (10+11+13+14) + (20+21+23+24) = 8 + 48 + 88 = 144
    return sum;
}

int test_triple_nested_break() {
    // Triple nested loops with break in innermost
    int sum = 0;
    int i = 0;
    while (i < 2) {
        int j = 0;
        while (j < 2) {
            int k = 0;
            while (k < 5) {
                if (k >= 2) {
                    break;  // Should break innermost loop only
                }
                sum = sum + (i * 100) + (j * 10) + k;
                k++;
            }
            j++;
        }
        i++;
    }
    // Should execute: i=0,j=0,k=0,1  i=0,j=1,k=0,1  i=1,j=0,k=0,1  i=1,j=1,k=0,1
    // sum = (0+1) + (10+11) + (100+101) + (110+111) = 1 + 21 + 201 + 221 = 444
    return sum;
}

int test_triple_nested_continue() {
    // Triple nested loops with continue in innermost
    int sum = 0;
    int i = 0;
    while (i < 2) {
        int j = 0;
        while (j < 2) {
            int k = 0;
            while (k < 4) {
                k++;
                if (k == 2) {
                    continue;  // Should continue innermost loop only
                }
                sum = sum + (i * 100) + (j * 10) + k;
            }
            j++;
        }
        i++;
    }
    // Should execute: i=0,j=0,k=1,3,4  i=0,j=1,k=1,3,4  i=1,j=0,k=1,3,4  i=1,j=1,k=1,3,4
    // sum = (1+3+4) + (11+13+14) + (101+103+104) + (111+113+114) = 8 + 38 + 308 + 338 = 692
    return sum;
}

int test_nested_mixed_loops() {
    // Mix of for, while, and do-while with break/continue
    int sum = 0;
    for (int i = 0; i < 2; i++) {
        int j = 0;
        while (j < 3) {
            int k = 0;
            do {
                k++;
                if (k == 2) {
                    continue;  // Continue do-while
                }
                if (k >= 3) {
                    break;  // Break do-while
                }
                sum = sum + (i * 100) + (j * 10) + k;
            } while (k < 5);
            j++;
        }
    }
    // For each i,j: k executes 1,3 (skips 2, breaks at 3)
    // i=0,j=0,k=1  i=0,j=1,k=1  i=0,j=2,k=1  i=1,j=0,k=1  i=1,j=1,k=1  i=1,j=2,k=1
    // sum = 1 + 11 + 21 + 101 + 111 + 121 = 366
    return sum;
}

int main() {
    int result1 = test_nested_while_break_inner();
    int result2 = test_nested_while_continue_inner();
    int result3 = test_nested_for_break_inner();
    int result4 = test_nested_for_continue_inner();
    int result5 = test_triple_nested_break();
    int result6 = test_triple_nested_continue();
    int result7 = test_nested_mixed_loops();
    
    return result1 + result2 + result3 + result4 + result5 + result6 + result7;
    // 63 + 159 + 63 + 144 + 444 + 692 + 366 = 1931
}

