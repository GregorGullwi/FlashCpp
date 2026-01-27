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
    int second = get_second();
    int sum = sum_array();
    
    if (second == 20) {
        puts("get_second: PASS (20)");
    } else {
        puts("get_second: FAIL");
    }
    
    if (sum == 600) {
        puts("sum_array: PASS (600)");
    } else {
        puts("sum_array: FAIL");
    }
    
    return 0;
}
