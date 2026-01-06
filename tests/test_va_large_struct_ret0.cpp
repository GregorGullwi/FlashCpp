// Test variadic function with 16-byte struct arguments (System V AMD64 ABI)
// 9-16 byte structs are passed in TWO consecutive integer registers
// Returns: 0 on success

typedef char* va_list;

#define va_start(ap, param) __builtin_va_start(ap, param)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) ((void)(ap = 0))

struct Point3D {
    int x;
    int y;
    int z;
    int w;  // 16 bytes total
};

// Sum all coordinates of 16-byte struct arguments
int sum_points3d(int count, ...) {
    va_list args;
    va_start(args, count);
    
    int total = 0;
    for (int i = 0; i < count; i++) {
        Point3D p = va_arg(args, Point3D);
        total = total + p.x + p.y + p.z + p.w;
    }
    
    va_end(args);
    return total;
}

int main() {
    Point3D p1 = {1, 2, 3, 4};    // Sum = 10
    Point3D p2 = {5, 6, 7, 8};    // Sum = 26
    
    // Test: sum_points3d(2, p1, p2) should return 10 + 26 = 36
    int result = sum_points3d(2, p1, p2);
    
    if (result == 36) {
        return 0;
    }
    return 1;
}
