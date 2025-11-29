#define A 1
#if A && !1
#error "This should not be reached"
#endif

int main() {
    return 0;
}
