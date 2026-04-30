// Regression: pointer difference should return ptrdiff_t.
// On Windows x64 (LLP64), ptrdiff_t is long long (64-bit), not long (32-bit).
// This test uses a helper function with explicit return type to force the issue:
// The function returns a value that should be ptrdiff_t, but if incorrectly
// inferred as long (32-bit), truncation will occur when the value is returned.

long get_diff(const char* a, const char* b) {
    return a - b;
}

int main() {
    const char buf[32] = {};
    long result = get_diff(buf + 16, buf);
    if (result != 16) return 1;
    return 0;
}