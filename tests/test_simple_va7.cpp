typedef char* va_list;
extern "C" void __cdecl __va_start(va_list*, ...);

int sum_ints(int count, ...) {
    va_list args;
    __va_start(&args, count);
    return 0;
}

int main() {
    return 0;
}
