// Test destructor

struct Counter {
    int value;
    int destructorCalled;

    Counter() {
        value = 42;
        destructorCalled = 0;
    }

    ~Counter() {
        destructorCalled = 1;
    }
};

int main() {
    Counter c;
    {
        Counter c2;
    }
    // Destructor should have been called when c2 went out of scope
    // But c's destructor hasn't been called yet
    return c.destructorCalled;  // Should return 0 (c's destructor not called yet)
}

