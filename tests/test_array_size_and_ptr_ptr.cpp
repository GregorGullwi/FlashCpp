// Test static array size and pointer-to-pointer

extern "C" int printf(const char* fmt, ...);

// Test 1: Return size of static array via sizeof
int get_array_size() {
    int arr[5];
    arr[0] = 1;
    arr[1] = 2;
    arr[2] = 3;
    arr[3] = 4;
    arr[4] = 5;
    return sizeof(arr) / sizeof(arr[0]);
}

// Test 2: Function taking pointer-to-pointer
void set_via_ptr_ptr(int** pp, int* new_val) {
    *pp = new_val;
}

// Test 3: Access via pointer-to-pointer
int get_via_ptr_ptr(int** pp) {
    return **pp;
}

int main() {
    // Test 1: Static array size
    int size = get_array_size();
    printf("Array size: %d (expected 5)\n", size);
    
    // Test 2 & 3: Pointer-to-pointer
    int value = 42;
    int* ptr = &value;
    int** pptr = &ptr;
    
    int result = get_via_ptr_ptr(pptr);
    printf("Via **: %d (expected 42)\n", result);
    
    // Test 4: Modify pointer via pointer-to-pointer
    int other_value = 99;
    set_via_ptr_ptr(pptr, &other_value);
    printf("After set_via_ptr_ptr: %d (expected 99)\n", *ptr);
    
    // Return success indicator
    return (size == 5 && result == 42 && *ptr == 99) ? 0 : 1;
}
