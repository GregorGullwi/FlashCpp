extern "C" int printf(const char* fmt, ...);

double gval = 3.14159;
double arr[3] = {1.5, 2.5, 3.5};

int main() {
    printf("gval = %f\n", gval);
    printf("arr = %f %f %f\n", arr[0], arr[1], arr[2]);
    return 0;
}
