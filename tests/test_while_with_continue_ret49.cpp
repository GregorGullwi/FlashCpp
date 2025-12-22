int test_while_with_continue() {
    int sum = 0;
    int i = 0;
    while (i < 10) {
        i = i + 1;
        if (i == 2) {
            continue; // Skip when i is 2
        }
        if (i == 4) {
            continue; // Skip when i is 4
        }
        sum = sum + i;
    }
    return sum; // Should be 49 (1+3+5+6+7+8+9+10)
}

int main() {
    return test_while_with_continue();
}

