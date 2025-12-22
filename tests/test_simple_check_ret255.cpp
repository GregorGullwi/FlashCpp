// Simple test

int test_one() {
    int a = 5;
    int b = 3;
    int c = 2;
    int result = (a + b) * c - (a - b) / c;
    return result;
}

int main() {
    int r1 = test_one();
    return r1;
}

