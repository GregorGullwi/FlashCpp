// Test __is_constructible

int test_is_constructible() {
    int result = 0;
    // Scalars are default constructible
    if (__is_constructible(int)) result += 1;
    if (__is_constructible(double)) result += 2;
    // Scalars are copy constructible
    if (__is_constructible(int, int)) result += 4;
    
    return result;  // Expected: 7
}

int main() {
    return test_is_constructible();
}
