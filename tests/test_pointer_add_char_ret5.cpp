// Test pointer addition with char* (element size = 1)
int main() {
    char arr[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    char* p = arr;
    char* q = p + 5;  // q points to arr[5]
    
    long diff = q - p;  // Should be 5
    return (int)diff;
}
