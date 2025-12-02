#define NO_PTR_CAST(x, type) ((type)(x))

int test() {
    char* ptr = nullptr;
    int result = NO_PTR_CAST(5, int);
    return 0;
}

int main() {
    return 0;
}
