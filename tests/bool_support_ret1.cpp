bool test_bool_literals() {
    bool a = true;
    bool b = false;
    return a && !b;
}

bool test_bool_operations(bool x, bool y) {
    bool and_result = x && y;
    bool or_result = x || y;
    bool not_x = !x;
    return and_result || or_result || not_x;
}

bool test_bool_comparisons() {
    bool a = true;
    bool b = false;
    return (a == true) && (b == false) && (a != b);
}

int main() {
    return test_bool_literals() && test_bool_operations(true, false) && test_bool_comparisons();
}
