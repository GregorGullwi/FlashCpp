int test_basic_do_while() {
    int sum = 0;
    int i = 0;
    do {
        sum = sum + i;
        i = i + 1;
    } while (i < 10);
    return sum; // Should be 45 (0+1+2+...+9)
}

int main() {
    return test_basic_do_while();
}

