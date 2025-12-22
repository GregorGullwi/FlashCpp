int main() {
    int arr[2];
    arr[0] = 10;
    arr[1] = 20;
    
    int* ptr = &arr[0];
    ptr = ptr + 1;
    
    return *ptr;  // Should return 20
}
