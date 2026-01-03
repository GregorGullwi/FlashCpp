// Test AddressOf with long long index (64-bit signed)

struct Row {
    int cols[6];
};

int main() {
    Row matrix[4];
    
    // Initialize
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 6; ++j) {
            matrix[i].cols[j] = i * 10 + j;
        }
    }
    
    // Test with long long index (signed 64-bit)
    long long row = 2;
    long long col = 4;
    
    int* ptr = &matrix[row].cols[col];
    
    // Verify we got the correct address by checking the value
    // matrix[2].cols[4] should be 24
    if (*ptr != 24) {
        return 1;  // Failed - wrong value
    }
    
    // Modify through pointer
    *ptr = 888;
    
    // Verify modification worked
    if (matrix[2].cols[4] != 888) {
        return 2;  // Failed - modification didn't work
    }
    
    return 0;  // Success
}
