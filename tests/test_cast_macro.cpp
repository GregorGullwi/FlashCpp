#define CAST(x, type) ((type)(x))

int test() {
    int result = CAST(5, int);
    return result;
}

int main() {
    return 0;
}
