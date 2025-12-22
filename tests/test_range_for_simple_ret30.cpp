// Simplest possible range-for test

int main() {
    int arr[2];
    arr[0] = 10;
    arr[1] = 20;
    
    int sum = 0;
    for (int x : arr) {
        sum = sum + x;
    }
    
    return sum;  // Expected: 30
}
