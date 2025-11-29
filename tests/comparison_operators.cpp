bool test_equal(int a, int b) {
    return a == b;
}

bool test_not_equal(int a, int b) {
    return a != b;
}

bool test_less_than(int a, int b) {
    return a < b;
}

bool test_less_equal(int a, int b) {
    return a <= b;
}

bool test_greater_than(int a, int b) {
    return a > b;
}

bool test_greater_equal(int a, int b) {
    return a >= b;
}

bool test_unsigned_comparison(unsigned int a, unsigned int b) {
    return (a < b) && (a <= b) && (a > b) && (a >= b);
}

int main() {
    bool result1 = test_equal(5, 5);
    bool result2 = test_not_equal(5, 3);
    bool result3 = test_less_than(3, 5);
    bool result4 = test_greater_than(5, 3);
    return result1 && result2 && result3 && result4;
}
