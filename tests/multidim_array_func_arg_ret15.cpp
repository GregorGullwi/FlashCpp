// Test for passing 2D array to function (decays to pointer)
// Expected return: 15

int sum_row(int* row, int cols) {
    int sum = 0;
    int i = 0;
    while (i < cols) {
        sum = sum + row[i];
        i = i + 1;
    }
    return sum;
}

int main() {
    int arr[2][3];
    arr[0][0] = 1;
    arr[0][1] = 2;
    arr[0][2] = 3;
    arr[1][0] = 4;
    arr[1][1] = 5;
    arr[1][2] = 6;
    
    // Pass pointer to first row
    int* row0 = &arr[0][0];
    int result = sum_row(row0, 3);  // 1 + 2 + 3 = 6
    
    // Pass pointer to second row
    int* row1 = &arr[1][0];
    result = result + sum_row(row1, 3);  // 6 + (4 + 5 + 6) = 6 + 15 = 21
    
    // Subtract to get a smaller return value (21 - 6 = 15)
    return result - 6;
}
