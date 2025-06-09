bool test_logical_and(bool a, bool b) {
    return a && b;
}

bool test_logical_or(bool a, bool b) {
    return a || b;
}

bool test_logical_not(bool a) {
    return !a;
}

bool test_complex_logic(bool a, bool b, bool c) {
    return (a && b) || (!a && c);
}

int main() {
    bool result1 = test_logical_and(true, true);
    bool result2 = test_logical_or(false, true);
    bool result3 = test_logical_not(false);
    bool result4 = test_complex_logic(true, false, true);
    return result1 && result2 && result3;
}
