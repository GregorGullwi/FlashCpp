// Expected return: 0 (when x=5, y=10, returns 10-10=0)
int main() {
    int x = 5;
    if (x > 0) {
        int y = x * 2;
        return y - 10;
    }
    return 1;
}
