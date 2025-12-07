// Diagnostic test to identify which assertion fails
struct TestStruct {
    int a;
    double b;
    char c;
};

int main() {
    int size_int = sizeof(int);
    if (size_int != 4) return 1;
    
    int size_double = sizeof(double);
    if (size_double != 8) return 2;
    
    int size_struct = sizeof(TestStruct);
    if (size_struct != 24) return 3;  // May fail due to padding
    
    int x = 42;
    int size_x = sizeof(x);
    if (size_x != 4) return 4;
    
    int size_expr = sizeof(x + 10);
    if (size_expr != 4) return 5;
    
    int arr[10];
    int size_arr = sizeof(arr);
    if (size_arr != 40) return 6;
    
    int align_int = alignof(int);
    if (align_int != 4) return 7;
    
    int align_double = alignof(double);
    if (align_double != 8) return 8;
    
    int align_struct = alignof(TestStruct);
    if (align_struct != 8) return 9;
    
    int align_x = alignof(x);
    if (align_x != 4) return 10;
    
    int result1 = sizeof(int) * 2;
    if (result1 != 8) return 11;
    
    int result2 = sizeof(int) + sizeof(double);
    if (result2 != 12) return 12;
    
    int result3 = alignof(int) * 2;
    if (result3 != 8) return 13;
    
    int result4 = alignof(double) - alignof(int);
    if (result4 != 4) return 14;
    
    return 0;  // All tests passed
}
