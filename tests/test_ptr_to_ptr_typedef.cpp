// Test pointer to typedef that is itself a pointer

extern "C" {
    typedef int A;
    typedef char* B;
    
    void func(B* param);
}

int main() {
    return 0;
}
