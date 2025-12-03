extern "C" int puts(const char* str);
extern "C" int printf(const char* fmt, ...);

int get_second() {
    int arr[] = {10, 20, 30};
    return arr[1];
}

int sum_array() {
    int arr[] = {100, 200, 300};
    return arr[0] + arr[1] + arr[2];
}

int main() {
    printf("arr[1] = %d (expected 20)\n", get_second());
    printf("sum = %d (expected 600)\n", sum_array());
    puts("Array tests completed - verify values above!");
    return 0;
}
