int main() {
    int arr[2];
    arr[0] = 10;
    arr[1] = 20;
    
    int* p1 = &arr[0];
    int* p2 = &arr[1];
    
    if (p1 != p2) {
        return 1;  // Pointers are different - correct
    }
    return 0;  // Should not reach here
}
