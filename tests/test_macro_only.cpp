#define va_arg(ap, type) (*(type*)((ap += 8) - 8))

int test() {
    char* args;
    int value = va_arg(args, int);
    return value;
}

int main() {
    return 0;
}
