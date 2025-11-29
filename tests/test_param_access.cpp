// Direct parameter access test - no printf

int get_first_param(int a, int b, int c) {
    return a;
}

int get_second_param(int a, int b, int c) {
    return b;
}

int get_third_param(int a, int b, int c) {
    return c;
}

int main() {
    int r1 = get_first_param(10, 20, 30);
    int r2 = get_second_param(10, 20, 30);
    int r3 = get_third_param(10, 20, 30);
    
    // Should return 0 if r1==10, r2==20, r3==30
    if (r1 == 10 && r2 == 20 && r3 == 30) {
        return 0;
    }
    return 1;
}
