// Test AddressOf with size_t index (64-bit unsigned on most platforms)

struct Data {
    int values[10];
};

int main() {
    Data arr[5];
    
    // Initialize
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 10; ++j) {
            arr[i].values[j] = i * 100 + j;
        }
    }
    
    // Test with size_t index (unsigned 64-bit)
    unsigned long long idx = 3;  // size_t equivalent
    int* ptr = &arr[2].values[idx];
    
    // Verify we got the correct address by checking the value
    // arr[2].values[3] should be 203
    if (*ptr != 203) {
        return 1;  // Failed - wrong value
    }
    
    // Modify through pointer
    *ptr = 999;
    
    // Verify modification worked
    if (arr[2].values[3] != 999) {
        return 2;  // Failed - modification didn't work
    }
    
    return 0;  // Success
}
