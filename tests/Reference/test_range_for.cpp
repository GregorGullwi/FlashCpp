// Test range-based for loops

int main() {
    int result = 0;
    
    // Test 1: Sum array elements using range-for
    int arr1[5];
    arr1[0] = 1;
    arr1[1] = 2;
    arr1[2] = 3;
    arr1[3] = 4;
    arr1[4] = 5;
    
    for (int x : arr1) {
        result = result + x;
    }
    // result = 1 + 2 + 3 + 4 + 5 = 15
    
    return result;  // Expected: 15
}
