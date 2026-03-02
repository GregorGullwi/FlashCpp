// Test that function pointer members use the correct return type in codegen
typedef int (*IntCallback)(int);

struct Handler {
    IntCallback callback;
};

int double_it(int x) {
    return x * 2;
}

int main() {
    Handler h;
    h.callback = double_it;
    int result = h.callback(21);
    return result;  // Should be 42
}
