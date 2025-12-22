int main() {
    int a = 5, b = 3, c = 8;
    if (a > b) {
        if (c > a) {
            return a + b + c;
        } else {
            return a + b;
        }
    } else {
        return 0;
    }
}

