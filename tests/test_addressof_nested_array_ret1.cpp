// Test nested array subscripts with AddressOf
// This tests: &arr[i].inner_arr[j]

struct Inner {
    int values[3];
};

struct Outer {
    int id;
    Inner inner;
};

int main() {
    Outer arr[2];
    
    // Initialize
    arr[0].id = 100;
    arr[0].inner.values[0] = 10;
    arr[0].inner.values[1] = 20;
    arr[0].inner.values[2] = 30;
    
    arr[1].id = 200;
    arr[1].inner.values[0] = 40;
    arr[1].inner.values[1] = 50;
    arr[1].inner.values[2] = 60;
    
    // Test: Get address of nested array element
    int* ptr = &arr[1].inner.values[2];
    
    // Verify we got the correct address by checking the value
    if (*ptr != 60) {
        return 2;  // Failed - wrong value (2 indicates first check failure)
    }
    
    // Modify through pointer
    *ptr = 999;
    
    // Verify modification worked
    if (arr[1].inner.values[2] != 999) {
        return 3;  // Failed - modification didn't work (3 indicates second check failure)
    }
    
    return 1;  // Success - matches _ret1 filename convention
}
