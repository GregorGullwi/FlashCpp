// Test without __cdecl

typedef char* va_list;

extern "C" {
    typedef unsigned long long uintptr_t;
    typedef char* va_list;
    
    // Function without __cdecl
    void __va_start(va_list*, ...);
}

int main() {
    return 0;
}
