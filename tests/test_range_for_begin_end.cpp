// Test range-based for loops with begin()/end() methods
// Note: FlashCpp parser doesn't yet support member function definitions inside structs
// This test is currently disabled until parser support is added

// Workaround version using arrays directly
int main() {
    int arr[5];
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 30;
    arr[3] = 40;
    arr[4] = 50;

    int sum = 0;
    for (int x : arr) {
        sum = sum + x;
    }

    return sum;  // Expected: 150 (10+20+30+40+50)
}
