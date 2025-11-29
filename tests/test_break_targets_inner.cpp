// This test verifies that break targets the INNER loop, not the outer loop
// If break incorrectly targeted the outer loop, the result would be wrong

int test_break_inner_not_outer() {
    int outer_count = 0;
    int inner_count = 0;
    
    int i = 0;
    while (i < 3) {  // Outer loop should run 3 times
        outer_count++;
        
        int j = 0;
        while (j < 10) {  // Inner loop should break early
            inner_count++;
            if (j >= 1) {
                break;  // This MUST break inner loop only
            }
            j++;
        }
        
        i++;
    }
    
    // If break targets inner loop correctly:
    //   outer_count = 3 (outer loop runs 3 times)
    //   inner_count = 6 (inner loop runs 2 times per outer iteration: j=0,1)
    // If break incorrectly targets outer loop:
    //   outer_count = 1 (outer loop would exit on first iteration)
    //   inner_count = 2 (inner loop would run once: j=0,1)
    
    return (outer_count * 100) + inner_count;  // Should be 306, NOT 102
}

int test_continue_inner_not_outer() {
    int outer_count = 0;
    int inner_sum = 0;
    
    int i = 0;
    while (i < 2) {  // Outer loop runs 2 times
        outer_count++;
        
        int j = 0;
        while (j < 4) {  // Inner loop
            j++;
            if (j == 2) {
                continue;  // This MUST continue inner loop only
            }
            inner_sum = inner_sum + j;
        }
        
        i++;
    }
    
    // If continue targets inner loop correctly:
    //   outer_count = 2 (outer loop runs 2 times)
    //   inner_sum = (1+3+4) + (1+3+4) = 8 + 8 = 16
    // If continue incorrectly targets outer loop:
    //   outer_count = 2 (outer loop runs 2 times)
    //   inner_sum = 1 + 1 = 2 (would skip rest of inner loop)
    
    return (outer_count * 100) + inner_sum;  // Should be 216, NOT 202
}

int main() {
    int r1 = test_break_inner_not_outer();
    int r2 = test_continue_inner_not_outer();
    
    // Expected: 306 + 216 = 522
    // If targeting wrong loop: 102 + 202 = 304
    return r1 + r2;
}

