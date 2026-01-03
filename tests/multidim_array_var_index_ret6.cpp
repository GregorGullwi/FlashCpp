// Test for multidimensional arrays with variable indices
// Expected return: 6

int main() {
    // 2D array
    int arr2d[2][3];
    arr2d[0][0] = 1;
    arr2d[1][2] = 6;
    
    // Access with variable indices
    int i = 1;
    int j = 2;
    int elem = arr2d[i][j];  // Should be 6
    
    return elem;
}
