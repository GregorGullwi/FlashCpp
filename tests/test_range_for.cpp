// Test range-based for loop
int main() {
    int arr[5] = {1, 2, 3, 4, 5};
    int sum = 0;
    for (int x : arr) {
        sum = sum + x;
    }
    return sum;  // Should return 15
}
