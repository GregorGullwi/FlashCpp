// Test __cdecl calling convention

typedef char* va_list;

extern "C" {
    typedef unsigned long long uintptr_t;
    typedef char* va_list;
    
    // Function with __cdecl calling convention
    void __cdecl __va_start(va_list*, ...);
}

int main() {
    return 0;
}
