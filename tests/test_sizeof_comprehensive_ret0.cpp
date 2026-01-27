// Comprehensive test for sizeof() with arrays
int main() {
    // Test 1: sizeof with different primitive types
    int arr_int[10];
    char arr_char[20];
    long arr_long[5];
    
    int size_int = sizeof(arr_int);    // 10 * 4 = 40
    int size_char = sizeof(arr_char);  // 20 * 1 = 20
    int size_long = sizeof(arr_long);  // 5 * 8 = 40
    
    // Test 2: sizeof with variables used in expressions
    int total = size_int + size_char + size_long;  // 40 + 20 + 40 = 100
    
    // Test 3: nested usage
    int result = sizeof(arr_int) / sizeof(int);  // 40 / 4 = 10 (element count)
    
    // Return 0 if all tests pass
    return (size_int == 40 && size_char == 20 && size_long == 40 && 
            total == 100 && result == 10) ? 0 : 1;
}
