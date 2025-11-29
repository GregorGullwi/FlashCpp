int test_while_with_break() {
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

int main() {
    return test_while_with_break();
}

