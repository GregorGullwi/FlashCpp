// Test structured bindings with 2D array (1st dimension)
struct Matrix {
    int data[2][3];
};

int main() {
    Matrix m = {{{1, 2, 3}, {4, 5, 6}}};
    
    // Bind to first dimension (rows)
    auto [row1, row2] = m.data;
    
    // Access elements from each row
    int sum = row1[0] + row1[1] + row1[2] + row2[0] + row2[1] + row2[2];
    
    return sum - 3;  // 21 - 3 = 18
}
