extern "C" int printf(const char* fmt, ...);

float gval = 2.5f;
float arr[3] = {1.1f, 2.2f, 3.3f};

int main() {
    printf("gval = %f\n", gval);
    printf("arr = %f %f %f\n", arr[0], arr[1], arr[2]);
    return 0;
}
