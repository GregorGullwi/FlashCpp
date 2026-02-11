// Simplified test to identify issues
int test_char_and() {
    char c = 0x0F & 0x07;
    return c;
}

int main() {
    return test_char_and();  // Should return 7
}
