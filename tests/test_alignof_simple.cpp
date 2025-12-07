// Simple test for alignof operator
// NOTE: Returns 0 on success to avoid printf calling convention issues on Linux

int main() {
    int align_int = alignof(int);
    int align_double = alignof(double);
    
    int size_int = sizeof(int);
    int size_double = sizeof(double);
    
    // Return 0 if all values are correct
    return (align_int == 4 && align_double == 8 && 
            size_int == 4 && size_double == 8) ? 0 : 1;
}
