extern "C" int printf(const char* fmt, ...);
int global_var = 42;
int main() {
    printf("global_var = %d\n", global_var);
    return 0;
}
