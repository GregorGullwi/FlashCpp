// Test basic __try/__finally execution on normal exit
// This tests that __finally blocks execute when __try completes normally

int test_finally() {
    int result = 0;
    __try {
        result = 42;
    }
    __finally {
        result = result + 10;
    }
    return result;  // Should return 52
}

int main() {
    return test_finally();
}

