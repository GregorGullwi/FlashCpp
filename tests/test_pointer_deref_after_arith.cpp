int main() {
    int arr[2];
    arr[0] = 10;
    arr[1] = 20;
    
    int* ptr = &arr[0];
    ptr = ptr + 1;  // Move to arr[1]
    int val = *ptr;  // Dereference after arithmetic
    
    return val;  // Should return 20
}
