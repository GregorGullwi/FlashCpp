// Simple test to debug alignof
extern "C" int printf(const char*, ...);

int main() {
    int align_int = alignof(int);
    int align_double = alignof(double);
    
    printf("alignof(int) = %d (expected 4)\n", align_int);
    printf("alignof(double) = %d (expected 8)\n", align_double);
    
    int size_int = sizeof(int);
    int size_double = sizeof(double);
    
    printf("sizeof(int) = %d (expected 4)\n", size_int);
    printf("sizeof(double) = %d (expected 8)\n", size_double);
    
    return 0;
}
