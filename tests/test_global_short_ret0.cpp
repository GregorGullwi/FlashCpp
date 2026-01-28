extern "C" int printf(const char* fmt, ...);

short gval = 1234;
short arr[3] = {100, 200, 300};

int main() {
    printf("gval = %d\n", gval);
    printf("arr = %d %d %d\n", arr[0], arr[1], arr[2]);
    return 0;
}
