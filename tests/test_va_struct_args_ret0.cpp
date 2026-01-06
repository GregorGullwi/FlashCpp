// Test variadic function with struct arguments (System V AMD64)
// Small structs (≤8 bytes) passed by value in registers
// Returns: 0 on success

typedef char* va_list;

#define va_start(ap, param) __builtin_va_start(ap, param)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) ((void)(ap = 0))

struct Point {
    int x;
    int y;
};

// Sum the x and y coordinates of struct arguments
int sum_points(int count, ...) {
    va_list args;
    va_start(args, count);
    
    int total = 0;
    for (int i = 0; i < count; i++) {
        Point p = va_arg(args, Point);
        total = total + p.x + p.y;
    }
    
    va_end(args);
    return total;
}

int main() {
    Point p1 = {1, 2};
    Point p2 = {3, 4};
    Point p3 = {5, 6};
    
    // Test: sum_points(3, p1, p2, p3) should return 1+2+3+4+5+6 = 21
    int result = sum_points(3, p1, p2, p3);
    
    if (result == 21) {
        return 0;
    }
    return 1;
}
