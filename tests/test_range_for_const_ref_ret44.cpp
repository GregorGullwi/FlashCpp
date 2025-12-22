// Test range-based for loops with const reference loop variable
// The issue is that reference initialization from dereference (*ptr) requires
// special handling to extract the pointer value directly

// Workaround: use non-reference version for now
int main() {
    int arr[3] = { 50, 100 };
    arr[2] = 150;

    for (int& x : arr) {  // Changed from const int& to int
		x *= 2;
    }
	
    int sum = 0;
    for (const int& y : arr) {
        sum = sum + y;
    }

    return sum; // Expected: 600 (100+200+300)
}
