char char_func(char a, char b) {
    return a + b;
}

short short_func(short a, short b) {
    return a * b;
}

int mixed_func(char a, short b) {
    return a + b;
}

int main() {
    return char_func(10, 5) + short_func(20, 3) + mixed_func(1, 2);
}
