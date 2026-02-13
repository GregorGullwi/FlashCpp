// Test that ::identifier correctly resolves to the global scope,
// not a local variable that shadows it.

int x = 42;

int main() {
    int x = 0;
    return ::x;
}
