int test_is_reference() {
    int result = 0;
    if (__is_lvalue_reference(int&)) result += 1;
    if (__is_rvalue_reference(int&&)) result += 2;
    return result;
}

int main() {
    return 0;
}
