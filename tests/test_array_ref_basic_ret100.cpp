// Test if references to array elements work at all

int main() {
    int arr[3] = {10, 20, 30};
    
    // Test reference to array element (no compound assignment yet)
    int& ref = arr[1];
    ref = 100;  // Simple assignment
    
    // Result should be arr[1] = 100
    return arr[1];  // Should be 100
}
