// Minimal xvalue test
int consume_rvalue(int&& x) {
    return x + 10;
}

int main() {
    int value = 5;
    int result = consume_rvalue(static_cast<int&&>(value));
    return result - 15;  // Should be 0 for success
}
