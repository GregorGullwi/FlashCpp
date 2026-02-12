// Test: unscoped enum to integer implicit conversion
// Per C++20 [conv.prom]/4 and [conv.integral]
enum Color { Red = 0, Green = 1, Blue = 2 };
enum { UnnamedVal = 42 };

int take_int(int x) { return x; }
unsigned int take_uint(unsigned int x) { return x; }
long take_long(long x) { return x; }

int main() {
    // Named enum to int (promotion)
    int a = take_int(Red);
    
    // Named enum to unsigned int (conversion)
    unsigned int b = take_uint(Blue);
    
    // Unnamed enum to int (promotion) 
    int c = take_int(UnnamedVal);
    
    // Unnamed enum to unsigned int (conversion)
    unsigned int d = take_uint(UnnamedVal);
    
    // Named enum to long (conversion)
    long e = take_long(Green);
    
    // Result: 0 + 2 + 42 + 42 + 1 = 87
    return a + b + c + d + e - 87;
}
