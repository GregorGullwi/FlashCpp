// Test __builtin_strlen builtin function
// This is a compiler intrinsic that returns size_t (unsigned long on 64-bit)

int main() {
    const char* s = "hello";
    unsigned long len = __builtin_strlen(s);
    return (int)len;  // Returns 5 for "hello"
}
