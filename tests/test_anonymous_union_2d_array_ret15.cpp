// Test 2D array in anonymous union
struct Matrix {
    int mode;
    union {
        int value;
        int matrix[2][2];  // Smaller 2x2 matrix
    };
};

int main() {
    Matrix m;
    m.mode = 2;
    
    // Initialize a 2x2 matrix
    m.matrix[0][0] = 5;
    m.matrix[0][1] = 10;
    
    // Sum elements: 5+10 = 15
    int sum = m.matrix[0][0] + m.matrix[0][1];
    
    return sum;
}
