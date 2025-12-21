// Test multi-level member access with AddressOf
// This tests the one-pass address calculation for: &arr[i].member1.member2

struct Inner {
    int x;
    int y;
};

struct Outer {
    int a;
    Inner inner;
    int b;
};

int main() {
    Outer arr[3];
    
    // Initialize array
    arr[0].a = 10;
    arr[0].inner.x = 20;
    arr[0].inner.y = 30;
    arr[0].b = 40;
    
    arr[1].a = 50;
    arr[1].inner.x = 60;
    arr[1].inner.y = 70;
    arr[1].b = 80;
    
    arr[2].a = 90;
    arr[2].inner.x = 100;
    arr[2].inner.y = 110;
    arr[2].b = 120;
    
    // Test: Get address of nested member and verify value through pointer
    int* ptr = &arr[1].inner.y;
    
    // Verify we got the correct address by checking the value
    if (*ptr != 70) {
        return 1;  // Failed - wrong value
    }
    
    // Modify through pointer
    *ptr = 777;
    
    // Verify modification worked
    if (arr[1].inner.y != 777) {
        return 2;  // Failed - modification didn't work
    }
    
    return 0;  // Success
}
