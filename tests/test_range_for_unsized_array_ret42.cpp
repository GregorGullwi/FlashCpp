// Test: range-based for loop with unsized array (int arr[] = {...})
// Verifies that the array size is correctly inferred from the initializer
// for range-based for loops.

int main() {
    int arr[] = {10, 12, 20};
    int sum = 0;
    for (int x : arr) {
        sum += x;
    }
    // sum = 10 + 12 + 20 = 42
    return sum;
}
