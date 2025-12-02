typedef char* va_list;

#define va_arg(ap, type) (*(type*)((ap += 8) - 8))

int sum_ints(int count) {
    va_list args;
    return 0;
}

int main() {
    return 0;
}
