// Simple nested loop test to verify break/continue target correct loop

int test_nested_break() {
    int sum = 0;
    int i = 0;
    while (i < 2) {
        int j = 0;
        while (j < 3) {
            if (j >= 1) {
                break;  // Should break INNER loop only
            }
            sum = sum + 1;
            j++;
        }
        i++;
    }
    return sum; // Should be 2 (outer loop runs twice, inner loop runs once each time)
}

int test_nested_continue() {
    int sum = 0;
    int i = 0;
    while (i < 2) {
        int j = 0;
        while (j < 3) {
            j++;
            if (j == 2) {
                continue;  // Should continue INNER loop only
            }
            sum = sum + 1;
        }
        i++;
    }
    return sum; // Should be 4 (outer runs 2x, inner runs 3x but skips j==2, so 2 increments per outer)
}

int main() {
    int r1 = test_nested_break();
    int r2 = test_nested_continue();
    return r1 + r2; // 2 + 4 = 6
}

