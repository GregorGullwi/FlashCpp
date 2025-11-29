#define _CPPRTTI 1
#define _HAS_STATIC_RTTI 1

#if defined(_CPPRTTI) && !_HAS_STATIC_RTTI
#error "This should not be reached"
#endif

int main() {
    return 0;
}
