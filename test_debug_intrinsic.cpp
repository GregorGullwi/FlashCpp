int test_is_floating_point() {
    int result = 0;
    // These should be true
    if (__is_floating_point(float)) result += 1;
    if (__is_floating_point(double)) result += 2;
    
    // These should be false
    if (!__is_floating_point(int)) result += 4;
    if (!__is_floating_point(char)) result += 8;
    
    return result;  // Expected: 15
}

int main() {
    return test_is_floating_point();
}
