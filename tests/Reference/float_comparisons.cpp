bool test_float_equal(float a, float b) {
    return a == b;
}

bool test_float_not_equal(float a, float b) {
    return a != b;
}

bool test_float_less_than(float a, float b) {
    return a < b;
}

bool test_float_less_equal(float a, float b) {
    return a <= b;
}

bool test_float_greater_than(float a, float b) {
    return a > b;
}

bool test_float_greater_equal(float a, float b) {
    return a >= b;
}

bool test_double_comparisons(double a, double b) {
    return (a == b) && (a != b) && (a < b) && (a <= b) && (a > b) && (a >= b);
}

int main() {
    bool result1 = test_float_equal(3.14f, 3.14f);
    bool result2 = test_float_less_than(2.5f, 3.7f);
    bool result3 = test_double_comparisons(1.0, 2.0);
    return result1 && result2;
}
