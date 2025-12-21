// Test array indexing with int (32-bit signed) index - baseline test
int main() {
    int arr[10];
    
    // Initialize array
    for (int i = 0; i < 10; i++) {
        arr[i] = i * 20;
    }
    
    // Test 1: int index - basic indexing
    int idx1 = 0;
    int val1 = arr[idx1];
    
    // Test 2: int index - assignment
    int idx2 = 4;
    arr[idx2] = 333;
    
    // Test 3: int index - address computation
    int idx3 = 2;
    int* ptr = &arr[idx3];
    int val2 = *ptr;
    
    // Test 4: int with mid-range value
    int idx4 = 7;
    arr[idx4] = 222;
    int val3 = arr[idx4];
    
    // Return computed value
    return val1 + arr[idx2] + val2 + val3;
}
