int test() {
    int result = 0;
    if (__is_lvalue_reference(int&)) result += 1;
    return result;
}

int main() {
    return 0;
}
