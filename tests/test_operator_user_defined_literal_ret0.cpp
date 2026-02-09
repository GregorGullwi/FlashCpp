// Test user-defined literal operator parsing
struct MyStr {
    const char* data;
    unsigned long len;
};

inline MyStr operator""_ms(const char* str, unsigned long len) {
    MyStr s;
    s.data = str;
    s.len = len;
    return s;
}

int main() {
    return 0;
}
