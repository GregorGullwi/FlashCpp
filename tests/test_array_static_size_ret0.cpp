// Test passing arrays with static size vs pointer

extern "C" int printf(const char* fmt, ...);

// Function taking pointer (array decays)
int sum_ptr(int* arr, int size) {
    int sum = 0;
    for (int i = 0; i < size; i++) {
        sum = sum + arr[i];
    }
    return sum;
}

// Function taking static-sized array reference
int sum_static_3(int (&arr)[3]) {
    return arr[0] + arr[1] + arr[2];
}

// Function taking static-sized array (decays to pointer in C++)
int sum_array_3(int arr[3]) {
    return arr[0] + arr[1] + arr[2];
}

int main() {
    int arr[3];
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 30;
    
    // Test 1: Local array access (no function call)
    int local_sum = arr[0] + arr[1] + arr[2];
    printf("Local sum: %d (expected 60)\n", local_sum);
    
    // Test 2: Static-sized array parameter (decays to pointer)
    int static_sum = sum_array_3(arr);
    printf("Static array sum: %d (expected 60)\n", static_sum);
    
    // Test 3: Pointer parameter
    int ptr_sum = sum_ptr(arr, 3);
    printf("Pointer sum: %d (expected 60)\n", ptr_sum);
    
    // Test 4: Reference to static-sized array
    int ref_sum = sum_static_3(arr);
    printf("Reference sum: %d (expected 60)\n", ref_sum);
    
    // Return 0 on success (all sums should be 60)
    return (local_sum == 60 && static_sum == 60 && ptr_sum == 60 && ref_sum == 60) ? 0 : 1;
}