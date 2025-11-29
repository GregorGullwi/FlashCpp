int test_compare(int a) {
    if (a > 0) {
        return 1;
    }
    return 0;
}

int main() {
    int result = test_compare(5);
    return result;
}
