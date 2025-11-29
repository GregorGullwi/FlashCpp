// Minimal reproduction - two typedefs, then void

extern "C" {
    typedef int A;
    typedef int B;
    
    void func();
}

int main() {
    return 0;
}
