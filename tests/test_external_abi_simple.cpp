// Simpler test for debugging
extern "C" double external_mixed_params(int a, double b, int c, double d);

extern "C" int main() {
    double result = external_mixed_params(10, 20.5, 30, 40.5);
    // Just return 0 if it worked (we see the printf output)
    return 0;
}
