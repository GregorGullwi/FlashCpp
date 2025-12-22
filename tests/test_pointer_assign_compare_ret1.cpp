int main() {
    int arr[2];
    arr[0] = 10;
    arr[1] = 20;
    
    int* ptr = &arr[0];
    int* ptr2 = &arr[1];
    
    ptr = ptr + 1;  // Should now equal ptr2
    
    if (ptr == ptr2) {
        return 1;  // Success
    }
    return 0;  // Failed
}
