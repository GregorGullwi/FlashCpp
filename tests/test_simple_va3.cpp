typedef char* va_list;
extern "C" void __cdecl __va_start(va_list*, ...);

#define va_arg(ap, type) (*(type*)((ap += 8) - 8))
#define va_end(ap) (ap = (va_list)0)

int sum_ints(int count, ...) {
    va_list args;
    __va_start(&args, count);
    
    int total = 0;
    for (int i = 0; i < count; i++) {
        int value = va_arg(args, int);
        total += value;
    }
    
    va_end(args);
    return total;
}

int main() {
    return 0;
}
