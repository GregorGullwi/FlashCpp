typedef char* va_list;
extern "C" void __cdecl __va_start(va_list*, ...);

#define va_arg(ap, type) (*(type*)((ap += 8) - 8))
#define va_end(ap) (ap = (va_list)0)

int sum_ints(int count, ...) {
    va_list args;
    __va_start(&args, count);
    
    va_end(args);
    return 0;
}

int main() {
    return 0;
}
