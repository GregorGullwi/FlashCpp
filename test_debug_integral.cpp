int test_is_integral() {
    int result = 0;
    // These should all be true
    if (__is_integral(int)) result += 1;
    if (__is_integral(char)) result += 2;
    if (__is_integral(bool)) result += 4;
    if (__is_integral(long)) result += 8;
    if (__is_integral(unsigned int)) result += 16;
    
    // These should be false
    if (!__is_integral(float)) result += 32;
    if (!__is_integral(double)) result += 64;
    
    return result;  // Expected: 127 (all tests pass)
}

int main() {
    return test_is_integral();
}
