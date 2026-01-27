// Test using typedef in parameter

extern "C" {
    typedef int A;
    typedef char* B;
    
    void func(B param);
}

int main() {
    return 0;
}
