int test_break() {
    int sum = 0;
    int i = 0;
    while (i < 100) {
        if (i >= 5) {
            break;
        }
        sum = sum + i;
        i = i + 1;
    }
    return sum; // Should be 10 (0+1+2+3+4)
}

int test_continue() {
    int sum = 0;
    int i = 0;
    while (i < 10) {
        i = i + 1;
        if (i == 2) {
            continue;
        }
        if (i == 4) {
            continue;
        }
        sum = sum + i;
    }
    return sum; // Should be 49 (1+3+5+6+7+8+9+10)
}

int main() {
    int result1 = test_break();
    int result2 = test_continue();
    return result1 + result2; // 10 + 49 = 59
}

