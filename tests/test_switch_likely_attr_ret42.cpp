// Test [[likely]]/[[unlikely]] attributes on switch/case labels (C++20)
// Expected return: 42

int main() {
    int x = 2;
    int result = 0;
    switch (x) {
        case 1: [[unlikely]] result = 10; break;
        case 2: [[likely]] result = 42; break;
        case 3: [[unlikely]] result = 100; break;
        default: [[unlikely]] result = 0; break;
    }
    return result;
}
