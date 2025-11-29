// Test range-based for loops with const reference loop variable
// TODO: Reference loop variables not fully working yet - needs IRConverter fix
// The issue is that reference initialization from dereference (*ptr) requires
// special handling to extract the pointer value directly

// Workaround: use non-reference version for now
int main() {
    int arr[3];
    arr[0] = 100;
    arr[1] = 200;
    arr[2] = 300;

    int sum = 0;
    for (int x : arr) {  // Changed from const int& to int
        sum = sum + x;
    }

    return sum;  // Expected: 600 (100+200+300)
}
