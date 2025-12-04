// Minimal test to debug __is_assignable

int test_is_assignable() {
    int result = 0;
    // Test with int, int
    if (__is_assignable(int, int)) result += 1;
    return result;
}

int main() {
    return test_is_assignable();
}
