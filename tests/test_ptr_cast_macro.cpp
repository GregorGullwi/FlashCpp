#define PTR_CAST(x, type) ((type*)(x))

int test() {
    char* ptr = nullptr;
    int* result = PTR_CAST(ptr, int);
    return 0;
}

int main() {
    return 0;
}
