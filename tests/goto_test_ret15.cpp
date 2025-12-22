int main() {
    int x = 5;
    goto skip;
    x = 100;  // This should be skipped
skip:
    x = x + 10;  // x should be 15, not 110
    return x;
}
