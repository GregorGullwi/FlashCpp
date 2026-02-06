// Test: Functions with __is_* prefix should not be confused with type traits
// This pattern occurs in libstdc++ where __is_single_threaded() is a regular function

inline bool __is_single_threaded() {
    return true;
}

int dispatch(int* mem, int val) {
    if (__is_single_threaded())
        return *mem + val;
    return 0;
}

int main() {
    int x = 10;
    int result = dispatch(&x, 5);
    if (result != 15) return 1;
    return 0;
}
