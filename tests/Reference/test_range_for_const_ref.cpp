// Test range-based for loops with const reference loop variable
// Note: FlashCpp parser doesn't yet support member function definitions inside structs
// This test is currently disabled until parser support is added

// Workaround version using arrays directly
int main() {
    int arr[3];
    arr[0] = 100;
    arr[1] = 200;
    arr[2] = 300;

    int sum = 0;
    for (const int& x : arr) {
        sum = sum + x;
    }

    return sum;  // Expected: 600 (100+200+300)
}
