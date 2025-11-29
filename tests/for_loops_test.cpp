int test_basic_for_loop() {
    int sum = 0;
    for (int i = 0; i < 5; i = i + 1) {
        sum += i;
    }
    return sum;
}

int main() {
    int result = test_basic_for_loop();
    return result;
}

