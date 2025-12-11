// Test __is_same compiler intrinsic
// This is a critical type trait used by <type_traits>

struct MyStruct {
    int value;
};

int main() {
    int result = 0;
    
    // Test same types - should be true
    if (__is_same(int, int)) result += 1;
    if (__is_same(double, double)) result += 2;
    if (__is_same(char, char)) result += 4;
    if (__is_same(MyStruct, MyStruct)) result += 8;
    
    // Test different types - should be false
    if (!__is_same(int, double)) result += 16;
    if (!__is_same(int, long)) result += 32;
    if (!__is_same(char, int)) result += 64;
    
    // Test with const qualifiers - const int != int
    if (!__is_same(const int, int)) result += 128;
    if (__is_same(const int, const int)) result += 256;
    
    // Test with pointers - int* != int
    if (!__is_same(int*, int)) result += 512;
    if (__is_same(int*, int*)) result += 1024;
    
    // Test with references - int& != int
    if (!__is_same(int&, int)) result += 2048;
    if (__is_same(int&, int&)) result += 4096;
    
    // Expected total: 1 + 2 + 4 + 8 + 16 + 32 + 64 + 128 + 256 + 512 + 1024 + 2048 + 4096 = 8191
    if (result == 8191) {
        return 0;  // Success
    }
    return 1;  // Failure
}
