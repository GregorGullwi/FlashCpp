// Test pointer arithmetic and array subscript with bounds checking

struct Point {
    int x, y;
    constexpr Point() : x(0), y(0) {}
    constexpr Point(int a, int b) : x(a), y(b) {}
};

// Test basic pointer arithmetic
constexpr int test_ptr_add() {
    int* arr = new int[5];
    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 30;
    arr[3] = 40;
    arr[4] = 50;
    
    int* p = arr + 2;  // Points to arr[2]
    int val = *p;      // Should be 30
    
    delete[] arr;
    return val;
}
static_assert(test_ptr_add() == 30, "Pointer arithmetic should work");

// Test pointer increment
constexpr int test_ptr_increment() {
    int* arr = new int[3];
    arr[0] = 100;
    arr[1] = 200;
    arr[2] = 300;
    
    int* p = arr;
    ++p;           // Now points to arr[1]
    int val = *p;  // Should be 200
    
    delete[] arr;
    return val;
}
static_assert(test_ptr_increment() == 200, "Pointer increment should work");

// Test array subscript
constexpr int test_array_subscript() {
    int* arr = new int[5];
    arr[0] = 1;
    arr[1] = 2;
    arr[2] = 3;
    arr[3] = 4;
    arr[4] = 5;
    
    int sum = arr[0] + arr[2] + arr[4];  // 1 + 3 + 5 = 9
    
    delete[] arr;
    return sum;
}
static_assert(test_array_subscript() == 9, "Array subscript should work");

// Test pointer arithmetic with offset
constexpr int test_ptr_offset() {
    int* arr = new int[10];
    for (int i = 0; i < 10; ++i) {
        arr[i] = i * 10;
    }
    
    int* p = arr + 5;
    int val = *p;  // Should be 50
    
    delete[] arr;
    return val;
}
static_assert(test_ptr_offset() == 50, "Pointer with offset should work");

// Test pointer subtraction
constexpr int test_ptr_diff() {
    int* arr = new int[10];
    int* p1 = arr + 2;
    int* p2 = arr + 7;
    
    long long diff = p2 - p1;  // Should be 5
    
    delete[] arr;
    return (int)diff;
}
static_assert(test_ptr_diff() == 5, "Pointer difference should work");

// Test combined arithmetic
constexpr int test_combined() {
    int* arr = new int[5];
    arr[0] = 5;
    arr[1] = 10;
    arr[2] = 15;
    arr[3] = 20;
    arr[4] = 25;
    
    int* p = arr;
    p += 2;        // Now at arr[2]
    int val1 = *p; // 15
    
    p++;           // Now at arr[3]
    int val2 = *p; // 20
    
    delete[] arr;
    return val1 + val2;
}
static_assert(test_combined() == 35, "Combined operations should work");

/* These should cause compile errors when uncommented:

// Out of bounds access
constexpr int test_out_of_bounds() {
    int* arr = new int[5];
    int bad = arr[10];  // ERROR: index 10 out of bounds (size 5)
    delete[] arr;
    return bad;
}

// Pointer arithmetic out of bounds
constexpr int test_ptr_out_of_bounds() {
    int* arr = new int[5];
    int* p = arr + 10;  // ERROR: offset 10 >= size 5
    delete[] arr;
    return 0;
}

// Negative index
constexpr int test_negative_index() {
    int* arr = new int[5];
    int bad = arr[-1];  // ERROR: negative index
    delete[] arr;
    return bad;
}

*/

int main() {
    return 0;
}
