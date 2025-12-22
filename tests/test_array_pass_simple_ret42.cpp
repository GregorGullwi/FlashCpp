// Minimal test for passing array to function

extern "C" int printf(const char* fmt, ...);

int get_first(int* arr) {
    return arr[0];
}

int main() {
    int arr[3];
    arr[0] = 42;
    arr[1] = 100;
    arr[2] = 200;
    
    int val = get_first(arr);
    printf("First element: %d (expected 42)\n", val);
    
    return val;
}
