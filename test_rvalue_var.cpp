int test() {
    int result = 0;
    bool is_rvalue = __is_rvalue_reference(int&&);
    if (is_rvalue) result += 2;
    return result;
}

int main() {
    return 0;
}
