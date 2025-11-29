// Test exact pattern from vadefs.h

typedef unsigned long long uintptr_t;
typedef char* va_list;

extern "C" {
    typedef unsigned long long uintptr_t;
    typedef char* va_list;
    
    void __cdecl __va_start(va_list*, ...);
}

int main() {
    return 0;
}
