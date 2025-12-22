int shift_left(int a, int b) {
    return a << b;
}

int shift_right(int a, int b) {
    return a >> b;
}

int main() {
    return shift_left(8, 2) + shift_right(32, 2);
}
