// Test pointer subtraction (ptr - ptr)
// Returns the number of elements between two pointers
int main() {
    int arr[5] = {10, 20, 30, 40, 50};
    int* p = arr;
    int* q = p + 3;  // q points to arr[3]
    
    long diff = q - p;  // Should be 3
    return (int)diff;
}
