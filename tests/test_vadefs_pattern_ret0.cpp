// Exact pattern from cstdio: vadefs.h

extern "C" {
    typedef unsigned long long uintptr_t;
    typedef char* va_list;
    
    void __va_start(va_list*, ...);
}

int main() {
    return 0;
}
