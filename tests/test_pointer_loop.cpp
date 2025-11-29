int main() {
    int arr[2];
    arr[0] = 10;
    arr[1] = 20;
    
    int* begin = &arr[0];
    int* end = &arr[2];
    
    int sum = 0;
    while (begin != end) {
        sum = sum + *begin;
        begin = begin + 1;
    }
    
    return sum;  // Expected: 30
}
