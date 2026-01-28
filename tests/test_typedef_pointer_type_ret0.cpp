// Test typedef of pointer type

extern "C" {
    typedef int A;
    typedef char* B;
    
    void func();
}

int main() {
    return 0;
}
