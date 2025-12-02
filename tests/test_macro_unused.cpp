#define va_arg(ap, type) (*(type*)((ap += 8) - 8))

int test() {
    char* args;
    return 0;
}

int main() {
    return 0;
}
