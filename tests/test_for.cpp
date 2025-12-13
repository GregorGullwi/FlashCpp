// EXPECTED_RETURN: 0
int main() {
    for (int i = 0; i < 10; i = i + 1) {
        return i;
    }
    return 0;
}

