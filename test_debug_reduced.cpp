int test_is_floating_point() {
    int result = 0;
    if (__is_floating_point(float)) result += 1;
    if (__is_floating_point(double)) result += 2;
    if (!__is_floating_point(int)) result += 4;
    if (!__is_floating_point(char)) result += 8;
    return result;
}

int test_is_pointer() {
    int result = 0;
    if (__is_pointer(int*)) result += 1;
    return result;
}

int main() {
    return 0;
}
