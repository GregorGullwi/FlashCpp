// Test AddressOf with unsigned int index

struct Point {
    int x;
    int y;
};

int main() {
    Point points[8];
    
    // Initialize
    for (int i = 0; i < 8; ++i) {
        points[i].x = i * 10;
        points[i].y = i * 20;
    }
    
    // Test with unsigned int index
    unsigned int idx = 5;
    int* ptr_y = &points[idx].y;
    
    // Verify we got the correct address by checking the value
    // points[5].y should be 100
    if (*ptr_y != 100) {
        return 1;  // Failed - wrong value
    }
    
    // Modify through pointer
    *ptr_y = 777;
    
    // Verify modification worked
    if (points[5].y != 777) {
        return 2;  // Failed - modification didn't work
    }
    
    return 0;  // Success
}
