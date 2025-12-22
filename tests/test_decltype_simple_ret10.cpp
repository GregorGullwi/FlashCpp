int main() {
    int x = 42;
    decltype(x) y = 10;
    return y;  // Should return 10
}
