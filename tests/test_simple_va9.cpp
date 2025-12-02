typedef char* va_list;

#define va_arg(ap, type) (*(type*)((ap += 8) - 8))

int sum_ints(int count) {
    va_list args;
    int value = va_arg(args, int);
    return value;
}

int main() {
    return 0;
}
