// Test pointer to typedef with variadic

extern "C" {
    typedef int A;
    typedef char* B;
    
    void func(B* param, ...);
}

int main() {
    return 0;
}
