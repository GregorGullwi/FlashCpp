// Test for multidimensional arrays
// Expected return: 30

int main() {
    // 2D array with constant indices
    int arr2d[2][3];
    arr2d[0][0] = 10;
    arr2d[1][2] = 20;
    
    // Read with constant indices
    int a = arr2d[0][0];  // 10
    int b = arr2d[1][2];  // 20
    
    return a + b;  // Should be 30
}
