// EXPECTED_RETURN: 45
int test_basic_while() {
    int sum = 0;
    int i = 0;
    while (i < 10) {
        sum = sum + i;
        i = i + 1;
    }
    return sum; // Should be 45 (0+1+2+...+9)
}

int main() {
    return test_basic_while();
}

